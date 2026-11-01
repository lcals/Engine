#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "plugins/editor/effects/effect_codec.hpp"
#include "plugins/editor/effects/effect_importer.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/effect_generated.h"
#include "runtime/utils/path_utils.hpp"

#include <Effekseer/IO/Effekseer.EfkEfcFactory.h>
#include <flatbuffers/flatbuffers.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using namespace engine;

bool expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::fprintf(stderr, "Effects pipeline: %.*s\n", static_cast<int>(message.size()),
                     message.data());
    }
    return condition;
}

bool write(const fs::path& path, std::string_view text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    return static_cast<bool>(stream);
}

std::string read(const fs::path& path)
{
    std::string text;
    return ResourceSystem::read_text(path, text) ? text : std::string{};
}

std::string identity(std::size_t index)
{
    char guid[37]{};
    std::snprintf(guid, sizeof(guid), "10000000-0000-4000-8000-%012zu", index);
    return guid;
}

std::string metadata(std::string_view guid, std::string_view importer, bool srgb = true)
{
    std::string result = "guid = \"" + std::string(guid) + "\"\nimporter = \"" +
                         std::string(importer) + "\"\nmeta_version = 1\n";
    if (importer == "texture")
    {
        result += "[texture]\nformat = \"rgba8\"\ncompression = \"uastc\"\n"
                  "compression_level = 0\nmipmaps = false\nsrgb = ";
        result += srgb ? "true\n" : "false\n";
    }
    return result;
}

struct Project
{
    fs::path root;
    std::size_t next_identity = 10;
    std::map<std::string, std::vector<Guid>> effect_dependencies;
    fs::path color_source;
    Guid color_guid;

    bool initialize()
    {
        std::error_code error;
        root = fs::temp_directory_path(error);
        if (error)
        {
            return false;
        }
        root /= "engine-effects-pipeline-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        for (const char* directory : {"assets", "settings", "cache"})
        {
            fs::create_directories(root / directory, error);
            if (error)
            {
                return false;
            }
        }
        return write(root / "settings/import.toml", "import_threads = 2\n");
    }

    bool copy_effect(const fs::path& source, const fs::path& fixture_assets,
                     std::string_view folder, const Guid& effect_guid)
    {
        ByteBuffer bytes;
        if (!ResourceSystem::read_binary(source, bytes) || bytes.empty())
        {
            return false;
        }
        Effekseer::EfkEfcProperty properties;
        if (!properties.Load(bytes.data(), static_cast<std::int32_t>(bytes.size())))
        {
            return false;
        }
        auto source_meta = source;
        source_meta += ".meta";
        const auto parsed = toml::parse(read(source_meta));
        if (!parsed || parsed.table()["resources"].as_array() == nullptr)
        {
            return false;
        }
        std::map<std::string, fs::path> fixture_paths;
        for (const auto& item : *parsed.table()["resources"].as_array())
        {
            const auto* table = item.as_table();
            if (table == nullptr)
            {
                return false;
            }
            const auto path = (*table)["path"].value<std::string>();
            const auto resource = (*table)["resource"].value<std::string>();
            if (!path || !resource || !resource->starts_with("project/") ||
                !fixture_paths
                     .emplace(*path, fixture_assets / PathUtils::from_utf8(resource->substr(8)))
                     .second)
            {
                return false;
            }
        }
        const auto destination = root / "assets" / folder;
        std::error_code error;
        fs::create_directories(destination, error);
        if (error || !fs::copy_file(source, destination / "sample.efkefc", error) || error)
        {
            return false;
        }
        std::string effect_meta = metadata(effect_guid, "effect");
        auto& dependencies = effect_dependencies[std::string(folder)];
        std::map<std::u16string, Guid> identities;
        const auto copy =
            [&](const std::vector<std::u16string>& paths, std::string_view importer, bool srgb)
        {
            for (const auto& path : paths)
            {
                if (identities.contains(path))
                {
                    continue;
                }
                const fs::path relative(path);
                const std::string source_name = PathUtils::to_utf8(relative);
                std::string lower = source_name;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char value)
                               {
                                   return static_cast<char>(
                                       value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value);
                               });
                const auto output = destination / PathUtils::from_utf8(lower);
                const auto fixture = fixture_paths.find(source_name);
                fs::create_directories(output.parent_path(), error);
                if (error || fixture == fixture_paths.end() ||
                    !fs::copy_file(fixture->second, output, error) || error)
                {
                    return false;
                }
                const auto guid = identity(next_identity++);
                auto meta = output;
                meta += ".meta";
                if (!write(meta, metadata(guid, importer, srgb)))
                {
                    return false;
                }
                dependencies.push_back(guid);
                identities.emplace(path, guid);
                effect_meta += "\n[[resources]]\npath = \"" + source_name +
                               "\"\nresource = \"project/" + std::string(folder) + "/" + lower +
                               "\"\n";
                if (color_source.empty() && importer == "texture" && srgb)
                {
                    color_source = output;
                    color_guid = guid;
                }
            }
            return true;
        };
        return copy(properties.GetColorImages(), "texture", true) &&
               copy(properties.GetNormalImages(), "texture", false) &&
               copy(properties.GetDistortionImages(), "texture", false) &&
               copy(properties.GetModels(), "source", false) &&
               write(destination / "sample.efkefc.meta", effect_meta);
    }

    ~Project()
    {
        if (!root.empty())
        {
            std::error_code error;
            fs::remove_all(root, error);
        }
    }
};

bool check_effect(const Project& project, const ResourceCatalog& catalog, std::string_view folder,
                  const Guid& guid, bool expect_model)
{
    const auto* entry = catalog.find(guid);
    const auto found = project.effect_dependencies.find(std::string(folder));
    if (!expect(entry != nullptr && entry->kind == schema::AssetKind::effect &&
                    found != project.effect_dependencies.end() &&
                    entry->dependencies.size() == found->second.size() &&
                    !entry->dependencies.empty(),
                "official effect has the effect kind and exact nonempty dependency list"))
    {
        return false;
    }
    bool has_model = false;
    for (const auto& dependency : found->second)
    {
        const auto* child = catalog.find(dependency);
        if (!expect(child != nullptr &&
                        std::find(entry->dependencies.begin(), entry->dependencies.end(),
                                  dependency) != entry->dependencies.end() &&
                        (child->kind == schema::AssetKind::texture ||
                         child->kind == schema::AssetKind::source),
                    "catalog contains every independent texture/model dependency"))
        {
            return false;
        }
        has_model = has_model || child->kind == schema::AssetKind::source;
    }
    ByteBuffer source;
    ByteBuffer payload;
    if (!expect(has_model == expect_model, "model sample retains its independent model resource") ||
        !expect(ResourceSystem::read_binary(project.root / "assets" / folder / "sample.efkefc",
                                            source) &&
                    ResourceSystem::read_resource(catalog, {{"content", project.root / "cache"}},
                                                  guid, payload),
                "read the published effect resource"))
    {
        return false;
    }
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                   payload.size());
    if (!expect(schema::VerifyEffectAssetDataBuffer(verifier), "effect wrapper verifies"))
    {
        return false;
    }
    const auto* effect = schema::GetEffectAssetData(payload.data());
    if (!expect(
            effect->data()->size() == source.size() &&
                std::equal(source.begin(), source.end(),
                           reinterpret_cast<const std::byte*>(effect->data()->data())) &&
                effect->bindings() != nullptr &&
                effect->bindings()->size() == entry->dependencies.size(),
            "wrapper preserves official bytes and maps every dependency without changing its path"))
    {
        return false;
    }
    for (const auto* binding : *effect->bindings())
    {
        std::string logical;
        if (!expect(binding != nullptr && binding->path() != nullptr &&
                        binding->guid() != nullptr &&
                        catalog.identities().guid_to_path(binding->guid()->str(), logical) &&
                        std::none_of(logical.begin(), logical.end(), [](unsigned char value)
                                     { return value >= 'A' && value <= 'Z'; }) &&
                        binding->kind() == catalog.find(binding->guid()->str())->kind,
                    "meta maps authored mixed-case paths to lowercase project resources by GUID"))
        {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    Project project;
    constexpr std::string_view scene_guid = "10000000-0000-4000-8000-000000000003";
    const auto laser_guid = identity(1);
    const auto model_guid = identity(2);
    if (!expect(argc == 2 && project.initialize(), "create temporary project"))
    {
        return 1;
    }
    const auto repository = PathUtils::from_utf8(argv[1]);
    const auto fixture_assets = repository / "projects/showcase/assets";
    if (!expect(project.copy_effect(fixture_assets / "effects/laser/laser.efkefc", fixture_assets,
                                    "laser", laser_guid) &&
                    project.copy_effect(fixture_assets / "effects/arrow/arrow.efkefc",
                                        fixture_assets, "model", model_guid),
                "copy the two public upstream fixtures and their mapped real textures/models"))
    {
        return 1;
    }
    const std::string scene = "name = \"Effects pipeline fixture\"\nactive_camera = 0\n"
                              "[[nodes]]\nid = 1\nname = \"Effect\"\nparent = 0\n"
                              "[[nodes.components]]\ntype = \"transform\"\n"
                              "[[nodes.components]]\ntype = \"effect\"\neffect = \"" +
                              laser_guid + "\"\nloop = true\n";
    if (!expect(write(project.root / "assets/test.scene", scene) &&
                    write(project.root / "assets/test.scene.meta", metadata(scene_guid, "scene")),
                "write scene using the effect plugin codec"))
    {
        return 1;
    }
    editor::AssetPipelineSystem pipeline;
    std::unique_ptr<editor::asset_pipeline::AssetImporter> importer =
        std::make_unique<plugins::effects::EffectImporter>();
    if (!expect(plugins::effects::register_effect_codec() && pipeline.initialize({}) &&
                    pipeline.add_importer(importer),
                "initialize optional effect importer and codec") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()))
    {
        return 1;
    }
    ResourceCatalog catalog;
    if (!expect(catalog.assign(pipeline.catalog()), "assign the published resource catalog") ||
        !check_effect(project, catalog, "laser", laser_guid, false) ||
        !check_effect(project, catalog, "model", model_guid, true))
    {
        return 1;
    }
    const auto* scene_entry = catalog.find(scene_guid);
    if (!expect(scene_entry != nullptr &&
                    scene_entry->dependencies == std::vector<Guid>{laser_guid},
                "scene discovers plugin effect references through the generic codec contract") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 0 && pipeline.last_build().reused > 2,
                "unchanged effects and dependencies are reused incrementally"))
    {
        return 1;
    }
    const auto revision = pipeline.last_build().revision;
    const auto published = read(project.root / "cache/gen" / laser_guid);
    const auto published_catalog = read(project.root / "cache/catalog.bin");
    const auto unchanged = [&]()
    {
        return !published.empty() && !published_catalog.empty() &&
               pipeline.last_build().revision == revision &&
               read(project.root / "cache/gen" / laser_guid) == published &&
               read(project.root / "cache/catalog.bin") == published_catalog;
    };
    auto color_meta = project.color_source;
    color_meta += ".meta";
    const auto original_meta = read(color_meta);
    if (!expect(!project.color_source.empty() && !original_meta.empty() &&
                    write(color_meta, metadata(project.color_guid, "texture", false)),
                "change an effect color image to an incompatible linear import") ||
        !expect(!pipeline.build_assets(project.root) &&
                    pipeline.last_error().find("color space") != std::string::npos && unchanged(),
                "invalid texture meta rejects the batch and preserves previous output") ||
        !expect(write(color_meta, original_meta), "restore texture import settings"))
    {
        return 1;
    }
    const auto held = project.root / "cache/held-texture.png";
    std::error_code error;
    fs::rename(project.color_source, held, error);
    if (!expect(!error, "temporarily remove a required effect image") ||
        !expect(!pipeline.build_assets(project.root) && unchanged(),
                "missing dependency rejects the batch and preserves previous output"))
    {
        return 1;
    }
    fs::rename(held, project.color_source, error);
    if (!expect(!error && pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 0 && pipeline.last_build().revision == revision,
                "restoring dependencies reuses the original valid cache"))
    {
        return 1;
    }
    pipeline.shutdown();
    return 0;
}
