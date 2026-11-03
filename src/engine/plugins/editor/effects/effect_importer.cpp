#include "plugins/editor/effects/effect_importer.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "plugins/runtime/effects/effect_validation.hpp"
#include "runtime/core/guid_map.hpp"
#include "runtime/schema/effect_generated.h"
#include "runtime/utils/path_utils.hpp"

#include <Effekseer/IO/Effekseer.EfkEfcFactory.h>
#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace engine::plugins::effects
{
namespace
{

constexpr std::uintmax_t maximum_effect_bytes = 64U * 1024U * 1024U;

bool inside(const std::filesystem::path& path, const std::filesystem::path& root)
{
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

bool normalize_reference(std::string text, std::string& output, std::string& error)
{
    std::replace(text.begin(), text.end(), '\\', '/');
    if (text.empty() || text.size() > 131068 ||
        std::any_of(text.begin(), text.end(), [](unsigned char value)
                    { return value < 0x20 || value == 0x7f || value == ':'; }))
    {
        error = "effect dependency requires a relative file path without control characters";
        return false;
    }
    const auto relative = PathUtils::from_utf8(text).lexically_normal();
    if (relative.is_absolute() || relative.has_root_name() || relative.filename().empty() ||
        relative == "." || relative.filename() == "..")
    {
        error = "effect dependency requires a relative file path";
        return false;
    }
    output = PathUtils::to_utf8(relative);
    return true;
}

bool dependency_name(const std::u16string& dependency, std::string& output, std::string& error)
{
    if (dependency.empty() || dependency.size() > 32767)
    {
        error = "effect dependency path is empty or too long";
        return false;
    }
    for (std::size_t index = 0; index < dependency.size(); ++index)
    {
        const auto unit = dependency[index];
        if (unit >= 0xd800 && unit <= 0xdbff)
        {
            if (++index >= dependency.size() || dependency[index] < 0xdc00 ||
                dependency[index] > 0xdfff)
            {
                error = "effect dependency path contains invalid UTF-16";
                return false;
            }
        }
        else if (unit >= 0xdc00 && unit <= 0xdfff)
        {
            error = "effect dependency path contains invalid UTF-16";
            return false;
        }
    }
    return normalize_reference(PathUtils::to_utf8(std::filesystem::path(dependency)), output,
                               error);
}

bool resource_remaps(const editor::asset_pipeline::ImportAsset& asset,
                     std::map<std::string, std::string>& output, std::string& error)
{
    auto meta = asset.source;
    meta += ".meta";
    toml::table root;
    if (!editor::asset_pipeline::read_table(meta, root, error))
    {
        return false;
    }
    if (!root.contains("resources"))
    {
        return true;
    }
    const auto* resources = root["resources"].as_array();
    if (resources == nullptr || resources->size() > 1024)
    {
        error = "effect meta resources must be an array of at most 1024 path/resource tables";
        return false;
    }
    for (const auto& item : *resources)
    {
        const auto* table = item.as_table();
        if (table == nullptr || table->size() != 2)
        {
            error = "each effect resources entry requires only path and resource";
            return false;
        }
        const auto path = (*table)["path"].value<std::string>();
        const auto resource = (*table)["resource"].value<std::string>();
        std::string normalized;
        if (!path || !resource || !GuidMap::valid_path(*resource) ||
            resource->find(':') != std::string::npos ||
            !normalize_reference(*path, normalized, error) ||
            !output.emplace(normalized, *resource).second)
        {
            if (error.empty())
            {
                error = "effect resource mapping requires unique relative paths and "
                        "assets-relative resources";
            }
            return false;
        }
    }
    return true;
}

bool dependency_path(const editor::asset_pipeline::ImportAsset& asset,
                     const editor::asset_pipeline::ImportContext& context,
                     const std::string& dependency,
                     const std::map<std::string, std::string>& remaps,
                     std::filesystem::path& source, std::string& logical, std::string& error)
{
    const auto remap = remaps.find(dependency);
    const auto root = context.assets.lexically_normal();
    if (remap != remaps.end())
    {
        logical = remap->second;
        source = (root / PathUtils::from_utf8(logical)).lexically_normal();
    }
    else
    {
        source = (asset.source.parent_path() / PathUtils::from_utf8(dependency)).lexically_normal();
        logical = PathUtils::to_utf8(source.lexically_relative(root));
    }
    if (!inside(source, root))
    {
        error = "effect dependency escapes the assets directory";
        return false;
    }
    std::error_code filesystem_error;
    const auto canonical_root = std::filesystem::canonical(root, filesystem_error);
    if (filesystem_error)
    {
        error = "cannot resolve the effect assets directory";
        return false;
    }
    const auto canonical_source = std::filesystem::canonical(source, filesystem_error);
    if (filesystem_error || !inside(canonical_source, canonical_root) ||
        !std::filesystem::is_regular_file(canonical_source, filesystem_error) || filesystem_error)
    {
        error = "effect dependency is missing, not a regular file, or escapes assets: " + logical;
        return false;
    }
    return true;
}

bool texture_colorspace(const std::filesystem::path& source, bool srgb, std::string& error)
{
    auto meta = source;
    meta += ".meta";
    toml::table root;
    if (!editor::asset_pipeline::read_table(meta, root, error))
    {
        return false;
    }
    const auto* settings = root["texture"].as_table();
    if (root.contains("texture") && settings == nullptr)
    {
        error = "effect texture meta requires a [texture] table";
        return false;
    }
    const auto extension = editor::asset_pipeline::source_extension(source);
    const bool hdr = extension == ".hdr" || extension == ".exr";
    srgb = srgb && !hdr;
    bool actual = !hdr;
    if (settings != nullptr && settings->contains("srgb"))
    {
        const auto value = (*settings)["srgb"].value_exact<bool>();
        if (!value)
        {
            error = "effect texture meta srgb must be boolean";
            return false;
        }
        actual = *value;
    }
    if (actual != srgb)
    {
        error = "effect texture color space mismatch: " + PathUtils::to_utf8(source) +
                (srgb ? " requires [texture].srgb=true" : " requires [texture].srgb=false");
        return false;
    }
    return true;
}

bool dependencies(const editor::asset_pipeline::ImportAsset& asset,
                  const editor::asset_pipeline::ImportContext& context,
                  const Effekseer::EfkEfcProperty& properties, std::vector<Guid>& output,
                  schema::EffectAssetDataT& data, std::string& error)
{
    if (!properties.GetMaterials().empty() || !properties.GetSounds().empty() ||
        !properties.GetCurves().empty())
    {
        error = "effects currently reject custom materials, sound and curve dependencies";
        return false;
    }
    std::map<std::string, std::string> remaps;
    std::set<std::string> used;
    if (!resource_remaps(asset, remaps, error))
    {
        return false;
    }
    const auto collect = [&](const std::vector<std::u16string>& paths, schema::AssetKind kind,
                             std::string_view extension, bool srgb)
    {
        for (const auto& path : paths)
        {
            std::filesystem::path source;
            std::string logical;
            Guid guid;
            std::string name;
            if (!dependency_name(path, name, error) ||
                !dependency_path(asset, context, name, remaps, source, logical, error))
            {
                return false;
            }
            if (!extension.empty() && editor::asset_pipeline::source_extension(source) != extension)
            {
                error = "effect dependency has an unsupported file type: " + logical;
                return false;
            }
            if (!context.identities.path_to_guid(logical, guid) ||
                !editor::asset_pipeline::reference(guid, kind, context, output, error))
            {
                error = "effect dependency is not an imported resource of the required type: " +
                        logical;
                return false;
            }
            if (kind == schema::AssetKind::texture && !texture_colorspace(source, srgb, error))
            {
                return false;
            }
            if (used.insert(name).second)
            {
                auto binding = std::make_unique<schema::EffectResourceBindingT>();
                binding->path = std::move(name);
                binding->guid = std::move(guid);
                binding->kind = kind;
                data.bindings.push_back(std::move(binding));
            }
        }
        return true;
    };
    if (!collect(properties.GetColorImages(), schema::AssetKind::texture, {}, true) ||
        !collect(properties.GetNormalImages(), schema::AssetKind::texture, {}, false) ||
        !collect(properties.GetDistortionImages(), schema::AssetKind::texture, {}, false) ||
        !collect(properties.GetModels(), schema::AssetKind::source, ".efkmodel", false))
    {
        return false;
    }
    for (const auto& [path, resource] : remaps)
    {
        static_cast<void>(resource);
        if (!used.contains(path))
        {
            error = "effect meta maps an undeclared dependency path: " + path;
            return false;
        }
    }
    std::sort(output.begin(), output.end());
    return true;
}

bool load(const editor::asset_pipeline::ImportAsset& asset,
          const editor::asset_pipeline::ImportContext& context, schema::EffectAssetDataT& data,
          std::vector<Guid>& references, std::string& error)
{
    std::error_code filesystem_error;
    const auto size = std::filesystem::file_size(asset.source, filesystem_error);
    if (filesystem_error || size == 0 || size > maximum_effect_bytes)
    {
        error = "effect source must be a readable .efkefc file within 64 MiB";
        return false;
    }
    std::vector<std::byte> bytes;
    if (!editor::asset_pipeline::read_bytes(asset.source, bytes, error) || bytes.empty() ||
        bytes.size() > maximum_effect_bytes)
    {
        if (error.empty())
        {
            error = "effect source changed while reading";
        }
        return false;
    }
    const auto byte_count = static_cast<std::int32_t>(bytes.size());
    const Effekseer::EfkEfcFile file(bytes.data(), byte_count);
    const auto runtime_data = file.ReadRuntimeData();
    if (!file.IsValid() || file.GetVersion() != 0 || runtime_data.data == nullptr ||
        runtime_data.size <= 0)
    {
        error = "effect requires a supported official .efkefc container with runtime data";
        return false;
    }
    Effekseer::EfkEfcProperty properties;
    if (!properties.Load(bytes.data(), byte_count))
    {
        error = "effect dependency information is malformed or uses an unsupported format";
        return false;
    }
    if (!dependencies(asset, context, properties, references, data, error) ||
        !validate_effect(bytes, error))
    {
        return false;
    }
    const auto* first = reinterpret_cast<const std::uint8_t*>(bytes.data());
    data.data.assign(first, first + bytes.size());
    return true;
}

} // namespace

std::string_view EffectImporter::id() const noexcept
{
    return "effect";
}

schema::AssetKind EffectImporter::kind() const noexcept
{
    return schema::AssetKind::effect;
}

bool EffectImporter::matches(const std::filesystem::path& source) const
{
    return editor::asset_pipeline::source_extension(source) == ".efkefc";
}

bool EffectImporter::inspect_batch(const editor::asset_pipeline::ImportContext&,
                                   std::string& fingerprint, std::string& error)
{
    if (!editor::asset_pipeline::schema_fingerprint({"effect.fbs"}, fingerprint, error))
    {
        return false;
    }
    fingerprint += "-effekseer-1808-resource-bindings-2";
    return true;
}

bool EffectImporter::scan_dependencies(editor::asset_pipeline::ImportAsset& asset,
                                       const editor::asset_pipeline::ImportContext& context,
                                       std::string& error) const
{
    asset.build_dependencies.clear();
    asset.runtime_dependencies.clear();
    schema::EffectAssetDataT data;
    std::vector<Guid> references;
    if (!load(asset, context, data, references, error))
    {
        return false;
    }
    asset.build_dependencies = references;
    asset.runtime_dependencies = std::move(references);
    return true;
}

bool EffectImporter::convert(const editor::asset_pipeline::ImportAsset& asset,
                             const editor::asset_pipeline::ImportContext& context,
                             std::string& error) const
{
    schema::EffectAssetDataT data;
    std::vector<Guid> references;
    if (!load(asset, context, data, references, error))
    {
        return false;
    }
    if (references != asset.build_dependencies || references != asset.runtime_dependencies)
    {
        error = "effect dependencies changed during import; retry the complete asset batch";
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishEffectAssetDataBuffer(builder, schema::EffectAssetData::Pack(builder, &data));
    return editor::asset_pipeline::write_bytes(
        context.output / asset.guid,
        std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())), error);
}

} // namespace engine::plugins::effects
