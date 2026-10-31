#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "plugins/editor/terrain/terrain_codec.hpp"
#include "plugins/editor/terrain/terrain_importer.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/terrain_generated.h"

#include <flatbuffers/verifier.h>
#include <toml++/toml.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using namespace engine;
constexpr std::string_view terrain_guid = "70000000-0000-4000-8000-000000000001";
constexpr std::string_view scene_guid = "70000000-0000-4000-8000-000000000002";

bool expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::fprintf(stderr, "Terrain pipeline: %.*s\n", static_cast<int>(message.size()),
                     message.data());
    }
    return condition;
}

bool write(const fs::path& path, std::string_view text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    return static_cast<bool>(file);
}

std::string metadata(std::string_view guid, std::string_view importer)
{
    return "guid = \"" + std::string(guid) + "\"\nimporter = \"" + std::string(importer) +
           "\"\nmeta_version = 1\n";
}

std::string terrain_source(int seed)
{
    return "cells_x = 16\ncells_y = 16\ncells_z = 16\nvoxel_size = 1\nchunk_cells = 16\n"
           "origin = [-8, -8, -8]\nheight = 0\namplitude = 2\nfrequency = 0.2\nseed = " +
           std::to_string(seed) + "\n[[cavities]]\ncenter = [0, -3, 0]\nradius = 2\n";
}

struct Project
{
    fs::path root;

    ~Project()
    {
        if (!root.empty())
        {
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }
    }

    bool initialize()
    {
        std::error_code error;
        root = fs::temp_directory_path(error);
        if (error)
        {
            root.clear();
            return false;
        }
        root /= "engine-terrain-pipeline-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        for (const auto* directory : {"assets", "settings", "cache"})
        {
            fs::create_directories(root / directory, error);
            if (error)
            {
                return false;
            }
        }
        return write(root / "settings/import.toml", "import_threads = 1\n");
    }
};

bool run(Project& project)
{
    const std::string scene = "name = \"Terrain test\"\nactive_camera = 0\n"
                              "[[nodes]]\nid = 1\nname = \"Terrain\"\nparent = 0\n"
                              "[[nodes.components]]\ntype = \"transform\"\n"
                              "[[nodes.components]]\ntype = \"terrain\"\nterrain = \"" +
                              std::string(terrain_guid) +
                              "\"\ncollision = true\nsurface_color = [0.3,0.5,0.2,1]\n"
                              "wall_color = [0.5,0.3,0.2,1]\nuv_scale = 0.25\n";
    if (!expect(write(project.root / "assets/test.terrain", terrain_source(1)) &&
                    write(project.root / "assets/test.terrain.meta",
                          metadata(terrain_guid, "terrain")) &&
                    write(project.root / "assets/test.scene", scene) &&
                    write(project.root / "assets/test.scene.meta", metadata(scene_guid, "scene")),
                "write terrain source and scene fixture"))
    {
        return false;
    }
    editor::AssetPipelineSystem pipeline;
    std::unique_ptr<editor::asset_pipeline::AssetImporter> importer =
        std::make_unique<plugins::terrain::TerrainImporter>();
    if (!expect(plugins::terrain::register_terrain_codec() && pipeline.initialize({}) &&
                    pipeline.add_importer(importer),
                "initialize terrain importer and component codec") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()))
    {
        return false;
    }
    ResourceCatalog catalog;
    ByteBuffer bytes;
    if (!expect(catalog.assign(pipeline.catalog()) &&
                    ResourceSystem::read_resource(catalog, {{"content", project.root / "cache"}},
                                                  terrain_guid, bytes),
                "read generated standalone terrain resource"))
    {
        return false;
    }
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size());
    const auto* terrain_entry = catalog.find(terrain_guid);
    const auto* scene_entry = catalog.find(scene_guid);
    if (!expect(schema::VerifyTerrainAssetDataBuffer(verifier) && terrain_entry != nullptr &&
                    terrain_entry->kind == schema::AssetKind::terrain &&
                    terrain_entry->dependencies.empty() && scene_entry != nullptr &&
                    scene_entry->dependencies == std::vector<Guid>{std::string(terrain_guid)},
                "terrain is independent and generic scene codec discovers its actual reference"))
    {
        return false;
    }
    const auto* volume = schema::GetTerrainAssetData(bytes.data());
    const std::size_t center = (8U * 17U + 5U) * 17U + 8U;
    if (!expect(volume->cells_x() == 16 && volume->density()->size() == 17U * 17U * 17U &&
                    volume->density()->Get(center) > 0,
                "source dimensions and underground sphere become sampled volume") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 0 && pipeline.last_build().reused >= 2,
                "unchanged source reuses published GUID resources"))
    {
        return false;
    }
    const auto first_hash = terrain_entry->content_hash;
    if (!expect(write(project.root / "assets/test.terrain", terrain_source(2)) &&
                    pipeline.build_assets(project.root),
                "changed generator parameter rebuilds the terrain") ||
        !expect(catalog.assign(pipeline.catalog()) && catalog.find(terrain_guid) != nullptr &&
                    catalog.find(terrain_guid)->content_hash != first_hash,
                "generator changes content while retaining resource GUID"))
    {
        return false;
    }
    ByteBuffer published;
    ByteBuffer published_catalog;
    if (!expect(
            ResourceSystem::read_binary(project.root / "cache/gen" / terrain_guid, published) &&
                ResourceSystem::read_binary(project.root / "cache/catalog.bin", published_catalog),
            "snapshot valid published batch"))
    {
        return false;
    }
    const auto revision = pipeline.last_build().revision;
    for (const auto& invalid : {terrain_source(2) + "\nunknown = 1\n",
                                std::string("cells_x = 15\n"), std::string("seed = 1.5\n"),
                                std::string("[[cavities]]\ncenter = [0,0,0]\nradius = -1\n")})
    {
        ByteBuffer current;
        ByteBuffer current_catalog;
        if (!expect(
                write(project.root / "assets/test.terrain", invalid) &&
                    !pipeline.build_assets(project.root) &&
                    pipeline.last_build().revision == revision &&
                    ResourceSystem::read_binary(project.root / "cache/gen" / terrain_guid,
                                                current) &&
                    ResourceSystem::read_binary(project.root / "cache/catalog.bin",
                                                current_catalog) &&
                    current == published && current_catalog == published_catalog,
                "invalid generation input preserves the previously published volume and catalog"))
        {
            return false;
        }
    }
    plugins::terrain::TerrainCodec codec;
    auto parsed =
        toml::parse("terrain = \"70000000-0000-4000-8000-000000000001\"\n"
                    "material = \"70000000-0000-4000-8000-000000000003\"\n"
                    "collision = true\nsurface_color = [0.3,0.4,0.2,1]\nuv_scale = 0.5\n");
    schema::ComponentDataT component;
    schema::ComponentDataT roundtrip;
    toml::table encoded;
    std::string error;
    if (!expect(parsed && codec.read(parsed.table(), component, error), "decode terrain component"))
    {
        return false;
    }
    codec.write(component, encoded);
    if (!expect(codec.read(encoded, roundtrip, error) && component == roundtrip,
                "component codec round trip retains GUIDs and settings"))
    {
        return false;
    }
    encoded.insert_or_assign("terrain", "project/test.terrain");
    if (!expect(!codec.read(encoded, roundtrip, error),
                "scene persistence requires GUID identity rather than unresolved paths"))
    {
        return false;
    }
    pipeline.shutdown();
    return true;
}
} // namespace

int main()
{
    Project project;
    return expect(project.initialize(), "create temporary terrain project") && run(project) ? 0 : 1;
}
