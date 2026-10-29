#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "editor/asset_pipeline/project_lock.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"
#include "runtime/core/guid_map.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/asset_catalog_generated.h"
#include "runtime/utils/path_utils.hpp"

#include <flatbuffers/verifier.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
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
        std::fprintf(stderr, "asset pipeline test: %s\n", message);
    }
    return value;
}

bool write(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << content;
    stream.close();
    return static_cast<bool>(stream);
}

std::string read(const std::filesystem::path& path)
{
    std::string output;
    if (!engine::ResourceSystem::read_text(path, output))
    {
        return {};
    }
    return output;
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

std::string scene()
{
    return std::string("name = \"Test scene\"\nactive_camera = \"1\"\n") +
           "[[nodes]]\nid = \"1\"\nname = \"Camera\"\n[[nodes.components]]\ntype = "
           "\"transform\"\nposition = [0, 0, 5]\n"
           "[[nodes.components]]\ntype = \"camera\"\n"
           "[[nodes]]\nid = \"18446744073709551615\"\nname = \"Cube\"\nparent = \"1\"\n"
           "[[nodes.components]]\ntype = \"transform\"\n"
           "[[nodes.components]]\ntype = \"mesh_renderer\"\n"
           "mesh = \"00000000-0000-4000-8000-000000000005\"\nmaterial = \"" +
           material_guid + "\"\n";
}

struct Fixture
{
    std::filesystem::path root;

    [[nodiscard]] bool initialize()
    {
        const auto value = std::chrono::steady_clock::now().time_since_epoch().count();
        std::error_code error;
        const auto temporary = std::filesystem::temp_directory_path(error);
        if (error)
        {
            return false;
        }
        root = temporary / ("engine-asset-pipeline-" + std::to_string(value));
        for (const char* directory : {"settings", "assets", "cache"})
        {
            std::filesystem::create_directories(root / directory, error);
            if (error)
            {
                return false;
            }
        }
        return true;
    }

    ~Fixture() noexcept
    {
        std::error_code error;
        if (!root.empty())
        {
            std::filesystem::remove_all(root, error);
        }
    }
};

bool load_catalog(const Fixture& fixture, engine::schema::AssetCatalogDataT& output)
{
    std::vector<std::byte> bytes;
    if (!engine::ResourceSystem::read_binary(fixture.root / "cache/catalog.bin", bytes))
    {
        return false;
    }
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size());
    if (!engine::schema::VerifyAssetCatalogDataBuffer(verifier))
    {
        return false;
    }
    engine::schema::GetAssetCatalogData(bytes.data())->UnPackTo(&output);
    return true;
}

std::string read_payload(const Fixture& fixture, std::string_view guid)
{
    engine::ResourceCatalog catalog;
    engine::ByteBuffer payload;
    if (!engine::ResourceSystem::read_catalog(fixture.root / "cache", catalog) ||
        !engine::ResourceSystem::read_resource(catalog, {{"content", fixture.root / "cache"}}, guid,
                                               payload))
    {
        return {};
    }
    return {reinterpret_cast<const char*>(payload.data()), payload.size()};
}

bool build(engine::editor::AssetPipelineSystem& pipeline, const Fixture& fixture)
{
    if (!pipeline.build_assets(fixture.root))
    {
        std::fprintf(stderr, "asset pipeline build: %s\n", pipeline.last_error().c_str());
        return false;
    }
    return true;
}

bool editor_guard(engine::editor::AssetPipelineSystem& pipeline, const Fixture& fixture)
{
    const auto hosts = fixture.root / "cache/editor-hosts";
    const auto runtime = fixture.root / "cache/editor-status";
    std::error_code code;
    std::filesystem::create_directories(hosts, code);
    if (!code)
    {
        std::filesystem::create_directories(runtime, code);
    }
    if (code)
    {
        return false;
    }
    std::ostringstream root_json;
    std::ostringstream runtime_json;
    root_json << std::quoted(engine::PathUtils::to_utf8(fixture.root));
    runtime_json << std::quoted(engine::PathUtils::to_utf8(runtime));
    const auto status = [&](std::string_view state, std::string_view session = "test")
    {
        return "{\"projectRoot\":" + root_json.str() + ",\"session\":\"" + std::string(session) +
               "\",\"projectState\":\"" + std::string(state) + "\",\"updatedAtMs\":0}";
    };
    const auto previous = read(fixture.root / "cache/catalog.bin");
    auto lease = std::make_unique<engine::editor::asset_pipeline::ProjectLock>();
    std::string error;
    if (!expect(lease->acquire(hosts / "test.lock", error), "editor owns lifetime lease") ||
        !expect(write(hosts / "test.json", "{\"projectRoot\":" + root_json.str() +
                                               ",\"runtimeDir\":" + runtime_json.str() + "}"),
                "write editor marker"))
    {
        return false;
    }
    std::filesystem::last_write_time(
        hosts / "test.json", std::filesystem::file_time_type::clock::now() - std::chrono::hours(1),
        code);
    engine::editor::asset_pipeline::ProjectLock probe;
    bool acquired = true;
    if (code ||
        !expect(probe.try_acquire(hosts / "test.lock", acquired, error) && !acquired &&
                    error.empty(),
                "lease contention is distinct from an inspection error") ||
        !expect(!probe.try_acquire(hosts / "missing/lock", acquired, error) && !acquired &&
                    !error.empty(),
                "unreadable lease path reports an error") ||
        !expect(!pipeline.build_assets(fixture.root) &&
                    read(fixture.root / "cache/catalog.bin") == previous,
                "live editor with old marker and missing status still prevents publication") ||
        !expect(write(runtime / "project-status.json", "invalid") &&
                    !pipeline.build_assets(fixture.root),
                "live editor with unreadable status never times out into write permission"))
    {
        return false;
    }
    for (const auto state : {"running", "starting", "stopping"})
    {
        if (!expect(write(runtime / "project-status.json", status(state)) &&
                        !pipeline.build_assets(fixture.root) &&
                        pipeline.last_error().find("Stop the active editor project") !=
                            std::string::npos &&
                        read(fixture.root / "cache/catalog.bin") == previous,
                    "paused or slow live editor cannot be overwritten despite an old heartbeat"))
        {
            return false;
        }
    }
    if (!expect(write(runtime / "project-status.json", status("idle", "previous-session")) &&
                    !pipeline.build_assets(fixture.root),
                "idle status from another session cannot authorize cache publication") ||
        !expect(write(runtime / "project-status.json", status("unknown")) &&
                    !pipeline.build_assets(fixture.root),
                "unknown live project state cannot authorize cache publication") ||
        !expect(write(runtime / "project-status.json", status("idle")) && build(pipeline, fixture),
                "matching idle session permits importing while editor remains open") ||
        !expect(write(runtime / "project-status.json", status("closed")) &&
                    build(pipeline, fixture),
                "matching closed session permits import during environment cleanup") ||
        !expect(write(runtime / "project-status.json", status("running")),
                "leave a running status to simulate process death"))
    {
        return false;
    }
    lease.reset();
    if (!expect(build(pipeline, fixture),
                "released crashed-editor lease allows import immediately") ||
        !expect(write(hosts / "test.json", "invalid") && build(pipeline, fixture),
                "unlocked crashed-editor marker content is irrelevant"))
    {
        return false;
    }
    std::filesystem::create_directory(hosts / "broken.lock", code);
    if (code ||
        !expect(write(hosts / "broken.json", "{}") && !pipeline.build_assets(fixture.root) &&
                    pipeline.last_error().find("cannot inspect editor host lease") !=
                        std::string::npos,
                "lease inspection failure never grants import permission"))
    {
        return false;
    }
    std::filesystem::remove_all(hosts, code);
    return !code;
}

bool tests()
{
    Fixture fixture;
    if (!expect(fixture.initialize(), "create project fixture directories"))
    {
        return false;
    }
    std::error_code filesystem_error;
    const auto assets = fixture.root / "assets";
    const auto cache = fixture.root / "cache";
    engine::editor::AssetPipelineSystem pipeline;
    const engine::EngineConfig config;
    if (!expect(!pipeline.build_assets(fixture.root), "uninitialized build rejected") ||
        !expect(pipeline.initialize(config), "initialize pipeline and Taskflow subsystem") ||
        !expect(pipeline.subsystem_count() == 1, "TaskSystem is owned as a System child") ||
        !expect(write(assets / "message.txt", "first"), "write source") ||
        !expect(write(assets / "cube.material", material(0.4)) &&
                    write(assets / "cube.material.meta", meta(material_guid, "material")),
                "write material fixture") ||
        !expect(write(assets / "main.scene", scene()) &&
                    write(assets / "main.scene.meta", meta(scene_guid, "scene")),
                "write scene fixture") ||
        !expect(build(pipeline, fixture), "first import"))
    {
        return false;
    }
    if (!expect(pipeline.last_build().scanned == 3 && pipeline.last_build().imported == 3 &&
                    pipeline.last_build().reused == 0 && pipeline.last_build().import_threads == 4,
                "first import uses default four threads and converts every source"))
    {
        return false;
    }
    engine::schema::AssetCatalogDataT catalog;
    if (!expect(load_catalog(fixture, catalog) && catalog.entries.size() == 3 &&
                    catalog.schema_version == 2 && pipeline.catalog() == catalog,
                "read generated FlatBuffers catalog"))
    {
        return false;
    }
    engine::Guid source_guid;
    std::set<engine::Guid> unique;
    for (const auto& entry : catalog.entries)
    {
        if (!expect(engine::GuidMap::valid_guid(entry->guid) && unique.insert(entry->guid).second,
                    "meta GUIDs are valid and unique") ||
            !expect(
                std::filesystem::is_regular_file(cache / "gen" / entry->guid, filesystem_error) &&
                    !filesystem_error,
                "generated filename is the complete GUID"))
        {
            return false;
        }
        if (entry->path == "project/message.txt")
        {
            source_guid = entry->guid;
        }
    }
    std::filesystem::directory_iterator generated(cache / "gen", filesystem_error);
    if (!expect(!filesystem_error, "open generated resources directory"))
    {
        return false;
    }
    const std::filesystem::directory_iterator end;
    while (generated != end)
    {
        const auto& entry = *generated;
        if (!expect(entry.path().extension().empty() &&
                        engine::GuidMap::valid_guid(
                            engine::PathUtils::to_utf8(entry.path().filename())),
                    "no suffix, prefix or version directory"))
        {
            return false;
        }
        generated.increment(filesystem_error);
        if (!expect(!filesystem_error, "iterate generated resources directory"))
        {
            return false;
        }
    }
    if (!expect(
            !source_guid.empty() &&
                std::filesystem::is_regular_file(assets / "message.txt.meta", filesystem_error) &&
                !filesystem_error,
            "first import creates source meta") ||
        !expect(std::filesystem::is_regular_file(cache / "assets.sqlite", filesystem_error) &&
                    !filesystem_error,
                "SQLite is in project cache"))
    {
        return false;
    }
    const auto first_revision = pipeline.last_build().revision;
    if (!expect(build(pipeline, fixture) && pipeline.last_build().imported == 0 &&
                    pipeline.last_build().reused == 3 &&
                    pipeline.last_build().revision == first_revision,
                "unchanged build reuses all GUID artifacts without new revision") ||
        !expect(write(assets / "message.txt", "second") && build(pipeline, fixture) &&
                    pipeline.last_build().imported == 1 && pipeline.last_build().reused == 2,
                "independent asset change only imports that asset") ||
        !expect(write(assets / "cube.material", material(0.7)) && build(pipeline, fixture) &&
                    pipeline.last_build().imported == 2 && pipeline.last_build().reused == 1,
                "material change schedules material and dependent scene"))
    {
        return false;
    }
    const auto stable_catalog = read(cache / "catalog.bin");
    const auto stable_material = read(cache / "gen" / material_guid);
    if (!expect(write(assets / "cube.material", material(4.0)) &&
                    !pipeline.build_assets(fixture.root),
                "invalid shader parameter blocks publication") ||
        !expect(read(cache / "catalog.bin") == stable_catalog &&
                    read(cache / "gen" / material_guid) == stable_material,
                "failed conversion preserves catalog and every old artifact") ||
        !expect(write(assets / "cube.material", material(0.7)), "restore valid source"))
    {
        return false;
    }
    const auto source_meta = read(assets / "message.txt.meta");
    if (!expect(write(assets / "duplicate.txt", "copied") &&
                    write(assets / "duplicate.txt.meta", source_meta) &&
                    !pipeline.build_assets(fixture.root),
                "duplicate GUID fails without rewriting identity"))
    {
        return false;
    }
    std::filesystem::remove(assets / "duplicate.txt", filesystem_error);
    std::filesystem::remove(assets / "duplicate.txt.meta", filesystem_error);
    std::filesystem::remove(assets / "message.txt.meta", filesystem_error);
    if (!expect(!pipeline.build_assets(fixture.root),
                "lost existing meta cannot silently receive new GUID") ||
        !expect(write(assets / "message.txt.meta", source_meta), "restore meta"))
    {
        return false;
    }
    std::filesystem::rename(assets / "message.txt", assets / "renamed.txt", filesystem_error);
    std::filesystem::rename(assets / "message.txt.meta", assets / "renamed.txt.meta",
                            filesystem_error);
    if (!expect(!filesystem_error && build(pipeline, fixture) && pipeline.last_build().removed == 0,
                "move file with meta preserves GUID") ||
        !expect(load_catalog(fixture, catalog), "read renamed catalog"))
    {
        return false;
    }
    bool mapped = false;
    for (const auto& entry : catalog.entries)
    {
        if (entry->guid == source_guid)
        {
            mapped = entry->path == "project/renamed.txt";
        }
    }
    if (!expect(mapped, "catalog maps preserved GUID to renamed logical path") ||
        !expect(write(cache / "gen" / source_guid, "corrupt") && build(pipeline, fixture) &&
                    pipeline.last_build().imported == 1 &&
                    read_payload(fixture, source_guid) == "second",
                "cache hit verifies bytes and rebuilds corrupted artifact") ||
        !expect(write(fixture.root / "settings/import.toml", "import_threads = 2\n") &&
                    build(pipeline, fixture) && pipeline.last_build().import_threads == 2 &&
                    pipeline.last_build().imported == 0,
                "project setting safely changes Taskflow worker count") ||
        !expect(write(fixture.root / "settings/import.toml", "import_threads = 0\n") &&
                    !pipeline.build_assets(fixture.root),
                "invalid worker configuration fails") ||
        !expect(write(fixture.root / "settings/import.toml", "import_threads = 4\n"),
                "restore workers"))
    {
        return false;
    }

    engine::schema::SceneDataT data;
    std::string import_error;
    const auto saved_scene = fixture.root / "settings/roundtrip.scene";
    if (!expect(engine::editor::SceneImporter::read(assets / "main.scene", data, import_error) &&
                    data.nodes[1]->info->id == std::numeric_limits<std::uint64_t>::max(),
                "TOML decimal strings preserve complete uint64 node identity") ||
        !expect(engine::editor::SceneImporter::write(saved_scene, data, import_error),
                "save actual generated scene data as TOML"))
    {
        return false;
    }
    engine::schema::SceneDataT roundtrip;
    if (!expect(engine::editor::SceneImporter::read(saved_scene, roundtrip, import_error) &&
                    data == roundtrip,
                "scene TOML save/read preserves all generated fields"))
    {
        return false;
    }
    data.nodes[0]->info->parent_id = data.nodes[1]->info->id;
    const auto saved_text = read(saved_scene);
    if (!expect(!engine::editor::SceneImporter::write(saved_scene, data, import_error) &&
                    read(saved_scene) == saved_text,
                "hierarchy cycle rejected without replacing saved scene"))
    {
        return false;
    }

    // 模拟目录交换后、SQL 提交前的进程退出，下一次持锁构建必须恢复。
    const auto before_recovery = read(cache / "catalog.bin");
    const auto staging = cache / ".asset-publish";
    std::filesystem::create_directories(staging, filesystem_error);
    std::filesystem::rename(cache / "gen", staging / "previous-gen", filesystem_error);
    std::filesystem::rename(cache / "catalog.bin", staging / "previous-catalog.bin",
                            filesystem_error);
    std::filesystem::create_directories(cache / "gen", filesystem_error);
    if (!expect(!filesystem_error && write(cache / "gen" / source_guid, "interrupted") &&
                    write(cache / "catalog.bin", "interrupted") &&
                    write(staging / "journal.toml",
                          "revision = \"11111111-1111-4111-8111-111111111111\"\nhad_gen = "
                          "true\n\"had_catalog.bin\" = true\n") &&
                    build(pipeline, fixture) && read(cache / "catalog.bin") == before_recovery &&
                    read_payload(fixture, source_guid) == "second",
                "interrupted publication rolls back before scanning"))
    {
        return false;
    }

    std::filesystem::remove(cache / "assets.sqlite", filesystem_error);
    if (!expect(!filesystem_error && build(pipeline, fixture) &&
                    pipeline.last_build().imported == 3 &&
                    read(assets / "renamed.txt.meta") == source_meta,
                "SQLite rebuild preserves meta GUID identities"))
    {
        return false;
    }
    std::filesystem::remove(assets / "renamed.txt", filesystem_error);
    std::filesystem::remove(assets / "renamed.txt.meta", filesystem_error);
    if (!expect(!filesystem_error && build(pipeline, fixture) &&
                    pipeline.last_build().removed == 1 &&
                    !std::filesystem::exists(cache / "gen" / source_guid, filesystem_error) &&
                    !filesystem_error,
                "deletion removes old GUID artifact and index") ||
        !expect(!pipeline.build_assets(fixture.root, "unknown-platform"),
                "invalid target cannot reuse wrong backend artifacts"))
    {
        return false;
    }
    if (!editor_guard(pipeline, fixture))
    {
        return false;
    }
    pipeline.shutdown();
    pipeline.shutdown();
    return true;
}

} // namespace

int main()
{
    return tests() ? 0 : 1;
}
