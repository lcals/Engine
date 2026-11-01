#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "plugins/editor/destruction/destruction_codec.hpp"
#include "plugins/editor/destruction/destruction_importer.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/destruction_generated.h"

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
constexpr std::string_view destruction_guid = "70000000-0000-4000-8000-000000000001";
constexpr std::string_view scene_guid = "70000000-0000-4000-8000-000000000002";

bool expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::fprintf(stderr, "Destruction pipeline: %.*s\n", static_cast<int>(message.size()),
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

std::string destruction_source(int slices)
{
    return "size = [4, 6, 2]\nfracture = \"slices\"\nslices_x = 2\nslices_y = " +
           std::to_string(slices) + "\nslices_z = 1\nbond_health = 1\nanchor_height = 0.05\n";
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
        root /= "engine-destruction-pipeline-" +
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
    const std::string scene = "name = \"Destruction test\"\nactive_camera = 0\n"
                              "[[nodes]]\nid = 1\nname = \"Destruction\"\nparent = 0\n"
                              "[[nodes.components]]\ntype = \"transform\"\n"
                              "[[nodes.components]]\ntype = \"destruction\"\ndestruction = \"" +
                              std::string(destruction_guid) +
                              "\"\ndensity = 100\nfriction = 0.7\nrestitution = 0.1\n";
    if (!expect(write(project.root / "assets/test.destruction", destruction_source(1)) &&
                    write(project.root / "assets/test.destruction.meta",
                          metadata(destruction_guid, "destruction")) &&
                    write(project.root / "assets/test.scene", scene) &&
                    write(project.root / "assets/test.scene.meta", metadata(scene_guid, "scene")),
                "write destruction source and scene fixture"))
    {
        return false;
    }
    editor::AssetPipelineSystem pipeline;
    std::unique_ptr<editor::asset_pipeline::AssetImporter> importer =
        std::make_unique<plugins::destruction::DestructionImporter>();
    if (!expect(plugins::destruction::register_destruction_codec() && pipeline.initialize({}) &&
                    pipeline.add_importer(importer),
                "initialize destruction importer and component codec") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()))
    {
        return false;
    }
    ResourceCatalog catalog;
    ByteBuffer bytes;
    if (!expect(catalog.assign(pipeline.catalog()) &&
                    ResourceSystem::read_resource(catalog, {{"content", project.root / "cache"}},
                                                  destruction_guid, bytes),
                "read generated standalone destruction resource"))
    {
        return false;
    }
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size());
    const auto* destruction_entry = catalog.find(destruction_guid);
    const auto* scene_entry = catalog.find(scene_guid);
    if (!expect(
            schema::VerifyDestructionAssetDataBuffer(verifier) && destruction_entry != nullptr &&
                destruction_entry->kind == schema::AssetKind::destruction &&
                destruction_entry->dependencies.empty() && scene_entry != nullptr &&
                scene_entry->dependencies == std::vector<Guid>{std::string(destruction_guid)},
            "destruction is independent and generic scene codec discovers its actual reference"))
    {
        return false;
    }
    const auto* data = schema::GetDestructionAssetData(bytes.data());
    if (!expect(data->chunks()->size() == 2 && data->bonds()->size() > 0,
                "official slices produce chunks and a connected support graph") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 0 && pipeline.last_build().reused >= 2,
                "unchanged source reuses published GUID resources"))
    {
        return false;
    }
    const auto first_hash = destruction_entry->content_hash;
    if (!expect(write(project.root / "assets/test.destruction", destruction_source(2)) &&
                    pipeline.build_assets(project.root),
                "changed generator parameter rebuilds the destruction") ||
        !expect(catalog.assign(pipeline.catalog()) && catalog.find(destruction_guid) != nullptr &&
                    catalog.find(destruction_guid)->content_hash != first_hash,
                "generator changes content while retaining resource GUID"))
    {
        return false;
    }
    ByteBuffer published;
    ByteBuffer published_catalog;
    if (!expect(
            ResourceSystem::read_binary(project.root / "cache/gen" / destruction_guid, published) &&
                ResourceSystem::read_binary(project.root / "cache/catalog.bin", published_catalog),
            "snapshot valid published batch"))
    {
        return false;
    }
    const auto revision = pipeline.last_build().revision;
    for (const auto& invalid :
         {destruction_source(2) + "\nunknown = 1\n", std::string("slices_x = 0\n"),
          std::string("seed = 1.5\n"), std::string("fracture = \"other\"\n"),
          std::string("size = [0, 2, 2]\n"), std::string("anchor_height = -1\n"),
          std::string("slices_x = 256\nslices_y = 2\n")})
    {
        ByteBuffer current;
        ByteBuffer current_catalog;
        if (!expect(
                write(project.root / "assets/test.destruction", invalid) &&
                    !pipeline.build_assets(project.root) &&
                    pipeline.last_build().revision == revision &&
                    ResourceSystem::read_binary(project.root / "cache/gen" / destruction_guid,
                                                current) &&
                    ResourceSystem::read_binary(project.root / "cache/catalog.bin",
                                                current_catalog) &&
                    current == published && current_catalog == published_catalog,
                "invalid generation input preserves the previously published volume and catalog"))
        {
            return false;
        }
    }
    plugins::destruction::DestructionCodec codec;
    auto parsed = toml::parse("destruction = \"70000000-0000-4000-8000-000000000001\"\n"
                              "material = \"70000000-0000-4000-8000-000000000003\"\n"
                              "density = 350\nfriction = 0.8\nrestitution = 0.15\n");
    schema::ComponentDataT component;
    schema::ComponentDataT roundtrip;
    toml::table encoded;
    std::string error;
    if (!expect(parsed && codec.read(parsed.table(), component, error),
                "decode destruction component"))
    {
        return false;
    }
    codec.write(component, encoded);
    if (!expect(codec.read(encoded, roundtrip, error) && component == roundtrip,
                "component codec round trip retains GUIDs and settings"))
    {
        return false;
    }
    encoded.insert_or_assign("destruction", "test.destruction");
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
    return expect(project.initialize(), "create temporary destruction project") && run(project) ? 0
                                                                                                : 1;
}
