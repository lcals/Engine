#include "editor/asset_build/asset_build_system.hpp"
#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{

constexpr const char* material_guid = "6b7b2290-b0a9-4652-812a-67e526381a10";
constexpr const char* scene_guid = "6b7b2290-b0a9-4652-812a-67e526381a11";

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "asset build test: %s\n", message);
    }
    return value;
}

bool write(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
    stream.close();
    return static_cast<bool>(stream);
}

std::string read(const std::filesystem::path& path)
{
    std::string text;
    if (!engine::ResourceSystem::read_text(path, text))
    {
        return {};
    }
    return text;
}

std::string meta(const char* guid, const char* importer)
{
    return std::string("guid = \"") + guid + "\"\nimporter = \"" + importer +
           "\"\nmeta_version = 1\n";
}

std::string material(double roughness)
{
    return "shader = \"00000000-0000-4000-8000-000000000001\"\n"
           "[[parameters]]\nname = \"roughness\"\ntype = \"float\"\nvalue = " +
           std::to_string(roughness) + "\n";
}

std::string manifest(std::string_view target = "")
{
    return "target_platform = \"" + std::string(target) +
           "\"\noutput = \"cache/build/game\"\n"
           "[[items]]\nname = \"main\"\nstorage = \"package\"\nroots = [\"project/main.scene\"]\n"
           "[[items]]\nname = \"extra\"\nstorage = \"files\"\nroots = [\"project/message.txt\"]\n";
}

struct Fixture
{
    std::filesystem::path root;

    bool initialize()
    {
        std::error_code code;
        root = std::filesystem::temp_directory_path(code);
        if (code)
        {
            return false;
        }
        root /= "engine-resource-build-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        for (const char* directory : {"settings", "assets", "cache"})
        {
            std::filesystem::create_directories(root / directory, code);
            if (code)
            {
                return false;
            }
        }
        const auto assets = root / "assets";
        return write(assets / "message.txt", "runtime data") &&
               write(assets / "unused.txt", "not selected") &&
               write(assets / "cube.material", material(0.4)) &&
               write(assets / "cube.material.meta", meta(material_guid, "material")) &&
               write(assets / "main.scene",
                     std::string("name = \"Build scene\"\nactive_camera = 1\n") +
                         "[[nodes]]\nid = 1\nname = \"Camera\"\n[[nodes.components]]\ntype = "
                         "\"transform\"\n"
                         "[[nodes.components]]\ntype = \"camera\"\n"
                         "[[nodes]]\nid = 2\nname = \"Cube\"\n[[nodes.components]]\ntype = "
                         "\"transform\"\n"
                         "[[nodes.components]]\ntype = \"mesh_renderer\"\nmesh = "
                         "\"00000000-0000-4000-8000-000000000005\"\nmaterial = \"" +
                         material_guid + "\"\n") &&
               write(assets / "main.scene.meta", meta(scene_guid, "scene")) &&
               write(root / "settings/resource-build.toml", manifest());
    }

    ~Fixture() noexcept
    {
        std::error_code code;
        if (!root.empty())
        {
            std::filesystem::remove_all(root, code);
        }
    }
};

bool build(engine::AssetBuildSystem& builder, const Fixture& fixture,
           const std::vector<std::string>& items)
{
    if (!builder.build(fixture.root, "settings/resource-build.toml", items))
    {
        std::fprintf(stderr, "resource build: %s\n", builder.last_error().c_str());
        return false;
    }
    return true;
}

bool tests()
{
    Fixture fixture;
    if (!expect(fixture.initialize(), "create project fixture"))
    {
        return false;
    }
    engine::AssetPipelineSystem pipeline;
    engine::AssetBuildSystem builder(pipeline);
    const engine::EngineConfig config;
    if (!expect(!builder.initialize(config), "builder requires initialized borrowed pipeline") ||
        !expect(pipeline.initialize(config) && builder.initialize(config),
                "initialize build systems") ||
        !expect(build(builder, fixture, {"main"}), "build one selected package"))
    {
        return false;
    }
    const auto output = fixture.root / "cache/build/game";
    engine::ResourceCatalog catalog;
    if (!expect(builder.last_build().resource_count == 2 &&
                    builder.last_build().package_count == 1 &&
                    builder.last_build().reused_package_count == 0,
                "package contains runtime closure only") ||
        !expect(pipeline.catalog().entries.size() == 4,
                "editor catalog retains all source assets") ||
        !expect(engine::ResourceSystem::read_catalog(output, catalog), "read packed metadata"))
    {
        return false;
    }
    const auto* scene = catalog.find(scene_guid);
    const auto* material_entry = catalog.find(material_guid);
    if (!expect(scene && material_entry &&
                    scene->location->path == material_entry->location->path &&
                    scene->location->offset != material_entry->location->offset,
                "multiple entities use disjoint ranges in one package"))
    {
        return false;
    }
    engine::ByteBuffer payload;
    if (!expect(engine::ResourceSystem::read_resource(catalog, {{"content", output}}, scene_guid,
                                                      payload) &&
                    !payload.empty(),
                "runtime reads and validates a package member") ||
        !expect(scene->dependencies.size() == 2 && material_entry->dependencies.size() == 1,
                "builtin mesh and shader dependencies remain explicit"))
    {
        return false;
    }
    const auto first_revision = builder.last_build().revision;
    std::error_code code;
    const auto original_time = std::filesystem::last_write_time(output / "catalog.bin", code);
    if (!expect(!code && build(builder, fixture, {"main"}) &&
                    builder.last_build().reused_package_count == 1 &&
                    builder.last_build().revision == first_revision &&
                    std::filesystem::last_write_time(output / "catalog.bin", code) ==
                        original_time &&
                    !code,
                "unchanged build reuses whole package and preserves catalog timestamp") ||
        !expect(write(fixture.root / "assets/cube.material", material(0.7)) &&
                    build(builder, fixture, {"main"}) &&
                    builder.last_build().reused_package_count == 0 &&
                    builder.last_build().revision != first_revision,
                "changed member rebuilds the entire package"))
    {
        return false;
    }
    if (!expect(build(builder, fixture, {"main", "extra"}) &&
                    builder.last_build().resource_count == 3 &&
                    builder.last_build().package_count == 1 &&
                    engine::ResourceSystem::read_catalog(output, catalog),
                "multiple items share one catalog with mixed independent and packed entities"))
    {
        return false;
    }
    engine::Guid message;
    if (!expect(
            catalog.identities().path_to_guid("project/message.txt", message) &&
                catalog.find(message)->location->path == "gen/" + message &&
                std::filesystem::path(catalog.find(message)->location->path).extension().empty(),
            "independent output retains complete GUID filename without suffix"))
    {
        return false;
    }
    const auto packed_path = catalog.find(scene_guid)->location->path;
    if (!expect(write(output / packed_path, read(output / packed_path) + "unindexed") &&
                    build(builder, fixture, {"main", "extra"}) &&
                    builder.last_build().reused_package_count == 0,
                "unindexed trailing package bytes force a complete rebuild"))
    {
        return false;
    }
    const auto stable_catalog = read(output / "catalog.bin");
    const auto stable_package = read(output / packed_path);
    const auto stable_stats = builder.last_build().revision;
    if (!expect(!builder.build(fixture.root, "settings/resource-build.toml",
                               std::vector<std::string>{"missing"}) &&
                    read(output / "catalog.bin") == stable_catalog &&
                    read(output / packed_path) == stable_package &&
                    builder.last_build().revision == stable_stats,
                "unknown item preserves prior publication") ||
        !expect(!builder.build(fixture.root, "settings/../resource-build.toml"),
                "manifest traversal rejected") ||
        !expect(write(fixture.root / "settings/resource-build.toml", manifest("android")) &&
                    build(builder, fixture, {"main", "extra"}) &&
                    engine::ResourceSystem::read_catalog(output, catalog) &&
                    catalog.data().target_platform == "android" &&
                    builder.last_build().reused_package_count == 0,
                "target platform participates in package reuse identity") ||
        !expect(write(fixture.root / "settings/resource-build.toml",
                      manifest() + "[[items]]\nname = \"conflict\"\nstorage = \"files\"\nroots = "
                                   "[\"project/main.scene\"]\n") &&
                    !builder.build(fixture.root, "settings/resource-build.toml"),
                "conflicting physical layouts rejected"))
    {
        return false;
    }
    builder.shutdown();
    builder.shutdown();
    pipeline.shutdown();
    return true;
}

} // namespace

int main()
{
    return tests() ? 0 : 1;
}
