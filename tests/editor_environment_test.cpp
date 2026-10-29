#include "editor/editor_environment.hpp"
#include "editor/project/project_system.hpp"
#include "runtime/app/app.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
namespace fs = std::filesystem;

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "editor environment test: %s\n", message);
    }
    return condition;
}

bool directory(const fs::path& path)
{
    std::error_code error;
    fs::create_directories(path, error);
    return !error;
}

bool absent(const fs::path& path)
{
    std::error_code error;
    const bool exists = fs::exists(path, error);
    return !error && !exists;
}

bool write(const fs::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    output.close();
    return static_cast<bool>(output);
}

std::string read(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

constexpr auto script_guid = "550e8400-e29b-41d4-a716-446655440000";

bool write_catalog(const fs::path& cache, const std::string& content)
{
    engine::schema::AssetCatalogDataT catalog;
    catalog.revision = "test-revision";
    catalog.target_platform = "macos";
    auto entry = std::make_unique<engine::schema::AssetEntryT>();
    entry->guid = script_guid;
    entry->path = "project/scripts/game.ts";
    entry->module_path = "scripts/game.js";
    entry->kind = engine::schema::AssetKind::script;
    entry->content_size = content.size();
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : content)
    {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
    entry->content_hash = text;
    catalog.entries.push_back(std::move(entry));
    flatbuffers::FlatBufferBuilder builder;
    engine::schema::FinishAssetCatalogDataBuffer(
        builder, engine::schema::AssetCatalogData::Pack(builder, &catalog));
    return write(
        cache / "catalog.bin",
        std::string(reinterpret_cast<const char*>(builder.GetBufferPointer()), builder.GetSize()));
}

bool run_cases(const fs::path& workspace)
{
    const auto root = workspace / "project";
    const auto persistent = workspace / "persistent";
    if (!directory(root / "settings") || !directory(root / "cache") || !directory(root / "assets"))
    {
        return false;
    }
    engine::ProjectSystem project(engine::PathUtils::to_utf8(root));
    engine::EditorEnvironment environment(project, persistent);
    const engine::EngineConfig config;
    if (!expect(!environment.initialize() && !engine::App::is_initialized(),
                "uninitialized project rejects environment without touching App") ||
        !expect(project.initialize(config), "initialize project") ||
        !expect(environment.initialize(), "initialize direct assets environment") ||
        !expect(engine::App::builtin_assets_path() == root / "assets",
                "App builtin points to assets, not project root") ||
        !expect(engine::App::persistent_data_path() == persistent, "map persistent directory") ||
        !expect(!environment.initialize(true) && environment.is_initialized(),
                "duplicate initialization preserves existing environment") ||
        !expect(!environment.prepare_builtin_assets(), "direct assets mode has no snapshot"))
    {
        return false;
    }
    project.shutdown();
    if (!expect(!environment.is_initialized(), "closed borrowed project invalidates environment"))
    {
        return false;
    }
    environment.shutdown();
    environment.shutdown();
    if (!expect(!engine::App::is_initialized(), "repeated shutdown closes owned App once") ||
        !expect(project.initialize(config) && environment.initialize(true),
                "restart project with snapshot environment"))
    {
        return false;
    }
    const auto snapshot = engine::App::builtin_assets_path();
    const auto other_session = snapshot.parent_path().parent_path() / "other-session";
    if (!expect(!environment.prepare_builtin_assets(), "missing compiled assets rejects copy") ||
        !expect(absent(snapshot), "failed copy leaves no partial assets") ||
        !directory(root / "cache" / "gen") || !directory(other_session) ||
        !write(other_session / "keep.txt", "other") ||
        !write(root / "cache" / "gen" / script_guid, "original") ||
        !write_catalog(root / "cache", "original") ||
        !write(persistent / "keep.txt", "persistent") ||
        !expect(environment.prepare_builtin_assets(), "prepare compiled asset snapshot") ||
        !expect(read(snapshot / "gen" / script_guid) == "original",
                "snapshot contains generated scripts") ||
        !write(root / "cache" / "gen" / script_guid, "replacement") ||
        !expect(!environment.prepare_builtin_assets(), "built snapshot cannot be overwritten") ||
        !expect(read(snapshot / "gen" / script_guid) == "original", "snapshot remains stable"))
    {
        return false;
    }
    environment.shutdown();
    if (!expect(absent(snapshot.parent_path()), "shutdown removes only owned session") ||
        !expect(read(other_session / "keep.txt") == "other", "preserve other session") ||
        !expect(read(persistent / "keep.txt") == "persistent", "preserve persistent content") ||
        !expect(read(root / "cache" / "gen" / script_guid) == "replacement",
                "preserve compiled cache") ||
        !expect(project.is_initialized(), "environment does not close borrowed project"))
    {
        return false;
    }

    engine::AppConfig external;
    external.application_name = "engine-environment-test";
    external.builtin_assets_path = root / "assets";
    external.persistent_data_path = workspace / "external-persistent";
    external.cache_path = workspace / "external-cache";
    if (!expect(engine::App::initialize(external), "initialize external App owner") ||
        !expect(!environment.initialize(true), "environment refuses another App owner"))
    {
        return false;
    }
    environment.shutdown();
    if (!expect(engine::App::is_initialized(), "rejected environment cannot close external App"))
    {
        return false;
    }
    engine::App::shutdown();

    const auto invalid_persistent = workspace / "persistent-file";
    if (!write(invalid_persistent, "file"))
    {
        return false;
    }
    engine::EditorEnvironment invalid(project, invalid_persistent);
    if (!expect(!invalid.initialize(true) && !engine::App::is_initialized(),
                "failed App initialization rolls back environment"))
    {
        return false;
    }
    std::error_code error;
    std::size_t session_count = 0;
    for (fs::directory_iterator entry(other_session.parent_path(), error), end;
         !error && entry != end; entry.increment(error))
    {
        ++session_count;
    }
    if (!expect(!error && session_count == 1, "failed initialization removes its snapshot"))
    {
        return false;
    }
    fs::path destructor_snapshot;
    {
        engine::EditorEnvironment scoped(project, persistent);
        if (!expect(scoped.initialize(true), "initialize scoped environment"))
        {
            return false;
        }
        destructor_snapshot = engine::App::builtin_assets_path().parent_path();
        engine::App::shutdown();
        if (!expect(!scoped.is_initialized() && !scoped.prepare_builtin_assets(),
                    "closed App invalidates environment and rejects asset preparation"))
        {
            return false;
        }
    }
    return expect(!engine::App::is_initialized() && absent(destructor_snapshot),
                  "destructor closes App and removes snapshot");
}
} // namespace

int main(int argc, char* argv[])
{
    if (argc != 2 || argv[1][0] == '\0')
    {
        return 2;
    }
    std::error_code error;
    const auto workspace = fs::absolute(fs::path(argv[1]) / "editor-environment-cases", error);
    if (error)
    {
        return 1;
    }
    fs::remove_all(workspace, error);
    if (error || !directory(workspace))
    {
        return 1;
    }
    const bool passed = run_cases(workspace);
    engine::App::shutdown();
    fs::remove_all(workspace, error);
    return passed && !error ? 0 : 1;
}
