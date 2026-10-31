#include "plugins/runtime/effects/effect_resource_path.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <filesystem>
#include <set>
#include <utility>

namespace engine::plugins::effects::detail
{

std::string effect_dependency_path(std::string_view path)
{
    std::string normalized(path);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    return PathUtils::to_utf8(PathUtils::from_utf8(normalized).lexically_normal());
}

bool read_effect_asset(ByteView bytes, const schema::EffectAssetData*& output, std::string& error)
{
    output = nullptr;
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size());
    if (!schema::VerifyEffectAssetDataBuffer(verifier))
    {
        error = "Invalid compiled effect resource";
        return false;
    }
    const auto* asset = schema::GetEffectAssetData(bytes.data());
    if (asset->data()->size() == 0)
    {
        error = "Compiled effect contains no Effekseer data";
        return false;
    }
    std::set<std::string> paths;
    if (asset->bindings())
    {
        for (const auto* binding : *asset->bindings())
        {
            const auto path = binding->path()->str();
            const auto relative = PathUtils::from_utf8(path);
            if (path.empty() || path != effect_dependency_path(path) || relative.is_absolute() ||
                relative.has_root_name() || relative.filename().empty() || relative == "." ||
                relative.filename() == ".." ||
                std::any_of(
                    path.begin(), path.end(), [](unsigned char character)
                    { return character < 0x20 || character == 0x7f || character == ':'; }) ||
                !GuidMap::valid_guid(binding->guid()->string_view()) ||
                (binding->kind() != schema::AssetKind::texture &&
                 binding->kind() != schema::AssetKind::source) ||
                !paths.insert(path).second)
            {
                error = "Invalid or duplicate compiled effect dependency binding";
                return false;
            }
        }
    }
    output = asset;
    return true;
}

bool load_effect_bindings(const Resource& resource, const schema::EffectAssetData& asset,
                          EffectBindings& output, std::string& error)
{
    output.clear();
    auto* resources = ResourceSystem::instance();
    if (!resources || resources->generation() != resource.generation())
    {
        error = "Effect dependencies require the effect's resource catalog generation";
        return false;
    }
    EffectBindings candidate;
    std::set<Guid> declared;
    if (asset.bindings())
    {
        for (const auto* binding : *asset.bindings())
        {
            const auto guid = binding->guid()->str();
            ResourcePtr dependency;
            if (std::find(resource.info().dependencies.begin(), resource.info().dependencies.end(),
                          guid) == resource.info().dependencies.end() ||
                !resources->loader().load(guid, dependency) || !dependency ||
                dependency->info().kind != binding->kind() ||
                dependency->generation() != resource.generation())
            {
                error = "Cannot load compiled effect dependency: " + guid;
                return false;
            }
            candidate.emplace(binding->path()->str(), std::move(dependency));
            declared.insert(guid);
        }
    }
    if (declared.size() != resource.info().dependencies.size())
    {
        error = "Effect bindings differ from the resource dependency list";
        return false;
    }
    output = std::move(candidate);
    return true;
}

std::string effect_utf8(const char16_t* path)
{
    return path != nullptr ? PathUtils::to_utf8(std::filesystem::path(path)) : std::string{};
}

} // namespace engine::plugins::effects::detail
