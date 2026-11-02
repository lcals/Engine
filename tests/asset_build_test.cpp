#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "plugins/editor/asset_build/asset_build_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/builtin_resources.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"

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

std::string manifest()
{
    return "output = \"cache/build/game\"\n"
           "[[items]]\nname = \"main\"\nstorage = \"package\"\nroots = [\"main.scene\"]\n"
           "[[items]]\nname = \"extra\"\nstorage = \"files\"\nroots = [\"message.txt\"]\n";
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
        root = std::filesystem::canonical(root, code);
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

bool build(engine::plugins::asset_build::AssetBuildSystem& builder, const Fixture& fixture,
           const std::vector<std::string>& items, std::string_view target = {})
{
    if (!builder.build(fixture.root, "settings/resource-build.toml", items, target))
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
    engine::ThreadSystem threads;
    engine::TaskSystem tasks(threads, {"Editor", 4, engine::TaskPriority::low});
    engine::editor::AssetPipelineSystem pipeline(tasks);
    engine::plugins::asset_build::AssetBuildSystem builder(pipeline);
    const engine::EngineConfig config;
    if (!expect(!builder.initialize(config), "builder requires initialized borrowed pipeline") ||
        !expect(threads.initialize(config) && tasks.initialize(config) &&
                    pipeline.initialize(config) && builder.initialize(config),
                "initialize build systems") ||
        !expect(build(builder, fixture, {"main"}), "build one selected package"))
    {
        return false;
    }
    const auto output = fixture.root / "cache/build/game";
    const auto builtin_count = engine::builtin_resource_catalog().entries.size();
    engine::ResourceCatalog catalog;
    if (!expect(builder.last_build().resource_count == 2 + builtin_count &&
                    builder.last_build().package_count == 2 &&
                    builder.last_build().reused_package_count == 0,
                "project package contains its runtime closure and a separate builtin package") ||
        !expect(pipeline.catalog().entries.size() == 4,
                "editor catalog retains all source assets") ||
        !expect(engine::ResourceSystem::read_catalog(output, catalog), "read packed metadata"))
    {
        return false;
    }
    for (const auto& builtin : engine::builtin_resource_catalog().entries)
    {
        const auto* entry = catalog.find(builtin->guid);
        engine::ByteBuffer bytes;
        if (!expect(entry && entry->location && entry->path == builtin->path &&
                        entry->kind == builtin->kind &&
                        entry->dependencies == builtin->dependencies &&
                        entry->location->path.starts_with("packs/builtin-") &&
                        engine::ResourceSystem::read_resource(catalog, {{"content", output}},
                                                              builtin->guid, bytes) &&
                        !bytes.empty(),
                    "all builtin identities have independently validated external package data"))
        {
            return false;
        }
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
                    builder.last_build().reused_package_count == 2 &&
                    builder.last_build().revision == first_revision &&
                    std::filesystem::last_write_time(output / "catalog.bin", code) ==
                        original_time &&
                    !code,
                "unchanged build reuses whole package and preserves catalog timestamp") ||
        !expect(write(fixture.root / "assets/cube.material", material(0.7)) &&
                    build(builder, fixture, {"main"}) &&
                    builder.last_build().reused_package_count == 1 &&
                    builder.last_build().revision != first_revision,
                "changed project member rebuilds its package and reuses the builtin package"))
    {
        return false;
    }
    if (!expect(build(builder, fixture, {"main", "extra"}) &&
                    builder.last_build().resource_count == 3 + builtin_count &&
                    builder.last_build().package_count == 2 &&
                    engine::ResourceSystem::read_catalog(output, catalog),
                "multiple items share one catalog with mixed independent and packed entities"))
    {
        return false;
    }
    engine::Guid message;
    if (!expect(
            catalog.identities().path_to_guid("message.txt", message) &&
                catalog.find(message)->location->path == "gen/" + message &&
                std::filesystem::path(catalog.find(message)->location->path).extension().empty(),
            "independent output retains complete GUID filename without suffix"))
    {
        return false;
    }
    const auto packed_path = catalog.find(scene_guid)->location->path;
    if (!expect(write(output / packed_path, read(output / packed_path) + "unindexed") &&
                    build(builder, fixture, {"main", "extra"}) &&
                    builder.last_build().reused_package_count == 1,
                "unindexed project bytes force that package rebuild while builtins are reused"))
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
        !expect(build(builder, fixture, {"main", "extra"}, "android") &&
                    engine::ResourceSystem::read_catalog(output, catalog) &&
                    catalog.data().target_platform == "android" &&
                    builder.last_build().reused_package_count == 0,
                "target platform participates in package reuse identity") ||
        !expect(write(fixture.root / "settings/resource-build.toml",
                      manifest() + "[[items]]\nname = \"conflict\"\nstorage = \"files\"\nroots = "
                                   "[\"main.scene\"]\n") &&
                    !builder.build(fixture.root, "settings/resource-build.toml"),
                "conflicting physical layouts rejected"))
    {
        return false;
    }
    if (!expect(write(fixture.root / "settings/resource-build.toml",
                      "target_platform = \"android\"\n" + manifest()) &&
                    !builder.build(fixture.root, "settings/resource-build.toml"),
                "removed manifest target field is rejected instead of read compatibly") ||
        !expect(write(fixture.root / "settings/resource-build.toml", "") &&
                    builder.build(fixture.root),
                "an empty manifest selects every imported project resource"))
    {
        return false;
    }
    const auto default_output = builder.last_build().output_path;
    if (!expect(default_output ==
                        fixture.root / "cache/build" / pipeline.catalog().target_platform &&
                    builder.last_build().resource_count == 4 + builtin_count &&
                    engine::ResourceSystem::read_catalog(default_output, catalog),
                "default publication is isolated by target and contains all project resources") ||
        !expect(write(fixture.root / "settings/resource-build.toml",
                      "[[items]]\nname = \"empty\"\nstorage = \"package\"\nroots = []\n") &&
                    builder.build(fixture.root) &&
                    builder.last_build().resource_count == builtin_count &&
                    builder.last_build().package_count == 1,
                "an explicit empty root list selects no project assets but retains builtins") ||
        !expect(write(fixture.root / "settings/resource-build.toml",
                      "[[items]]\nname = \"baseline\"\nstorage = \"package\"\n"
                      "roots = [\"builtin://materials/default-pbr\"]\n") &&
                    builder.build(fixture.root) &&
                    builder.last_build().resource_count == builtin_count &&
                    builder.last_build().package_count == 1,
                "explicit builtin roots resolve through the catalog and share its baseline"))
    {
        return false;
    }
    if (!expect(write(fixture.root / "settings/resource-build.toml",
                      "[[items]]\nname = \"incomplete\"\nstorage = \"files\"\n") &&
                    !builder.build(fixture.root),
                "missing roots does not silently become an empty project selection") ||
        !expect(write(fixture.root / "settings/resource-build.toml",
                      "[[items]]\nname = \"builtin\"\nstorage = \"package\"\nroots = []\n") &&
                    !builder.build(fixture.root),
                "project build items cannot claim the reserved builtin package name"))
    {
        return false;
    }
    std::filesystem::create_directories(fixture.root / "assets/project", code);
    engine::Guid folder_resource;
    engine::Guid unselected;
    if (!expect(
            !code && write(fixture.root / "assets/project/message.txt", "folder resource") &&
                write(fixture.root / "settings/resource-build.toml",
                      "[[items]]\nname = \"folder\"\nstorage = \"files\"\n"
                      "roots = [\"project/message.txt\"]\n") &&
                builder.build(fixture.root) &&
                engine::ResourceSystem::read_catalog(default_output, catalog) &&
                catalog.identities().path_to_guid("project/message.txt", folder_resource) &&
                !catalog.identities().path_to_guid("message.txt", unselected) &&
                builder.last_build().resource_count == builtin_count + 1,
            "project is an ordinary asset folder and is never stripped as an identity prefix") ||
        !expect(builder.build(fixture.root, "settings/missing.toml") &&
                    builder.last_build().resource_count == builtin_count + 5,
                "a missing explicit manifest selects all imported project resources") ||
        !expect(std::filesystem::remove(fixture.root / "settings/resource-build.toml", code) &&
                    !code && builder.build(fixture.root) &&
                    builder.last_build().resource_count == builtin_count + 5,
                "an absent default manifest selects all resources without compatibility lookup"))
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
