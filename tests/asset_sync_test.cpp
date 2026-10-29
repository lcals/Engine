#include "editor/asset_sync/asset_sync_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"

#include <nlohmann/json.hpp>

#include <array>
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
#include <utility>

namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

bool write_file(const fs::path& path, std::string_view text)
{
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::binary);
    output << text;
    output.close();
    return !error && !output.fail();
}

Json read_json(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return Json::parse(input, nullptr, false);
}

bool check(bool condition, const char* description)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
    return condition;
}

std::string source_guid(std::string_view name)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char value : name)
    {
        hash = (hash ^ value) * 1099511628211ULL;
    }
    char guid[37]{};
    std::snprintf(guid, sizeof(guid), "30000000-0000-4000-8000-%012llx",
                  static_cast<unsigned long long>(hash & 0xffffffffffffULL));
    return guid;
}

bool publish_scripts(const fs::path& source, const std::map<std::string, std::string>& scripts)
{
    engine::schema::AssetCatalogDataT catalog;
    catalog.schema_version = 2;
    catalog.target_platform = "android";
    catalog.revision = "sync-fixture";
    for (const auto& [name, text] : scripts)
    {
        auto entry = std::make_unique<engine::schema::AssetEntryT>();
        entry->guid = source_guid(name);
        entry->path = "project/" + name;
        entry->module_path = name;
        entry->kind = engine::schema::AssetKind::script;
        engine::ByteBuffer entity;
        if (!engine::ResourceSystem::encode_resource(
                entry->guid, entry->kind,
                engine::ByteView(reinterpret_cast<const std::byte*>(text.data()), text.size()),
                entity, entry->header_size))
        {
            return false;
        }
        std::uint64_t hash = 14695981039346656037ULL;
        for (const auto value : entity)
        {
            hash = (hash ^ std::to_integer<unsigned char>(value)) * 1099511628211ULL;
        }
        char digest[17]{};
        std::snprintf(digest, sizeof(digest), "%016llx", static_cast<unsigned long long>(hash));
        entry->content_hash = digest;
        entry->content_size = entity.size();
        entry->location = std::make_unique<engine::schema::FileLocationT>();
        entry->location->mount = "content";
        entry->location->path = "gen/" + entry->guid;
        entry->location->length = entity.size();
        catalog.revision += entry->content_hash;
        if (!write_file(
                source / entry->location->path,
                std::string_view(reinterpret_cast<const char*>(entity.data()), entity.size())))
        {
            return false;
        }
        catalog.entries.push_back(std::move(entry));
    }
    flatbuffers::FlatBufferBuilder builder;
    engine::schema::FinishAssetCatalogDataBuffer(
        builder, engine::schema::AssetCatalogData::Pack(builder, &catalog));
    return write_file(source / "catalog.bin",
                      std::string_view(reinterpret_cast<const char*>(builder.GetBufferPointer()),
                                       builder.GetSize()));
}

bool exercise(const fs::path& root)
{
    const auto source = root / "source";
    const auto runtime = root / "runtime";
    const auto available = runtime / "assets/available.json";
    const auto game_file = "gen/" + source_guid("scripts/game.js");
    const auto deleted_file = "gen/" + source_guid("scripts/deleted.js");
    std::map<std::string, std::string> scripts{
        {"scripts/game.js", "export default class Game {}\n"},
        {"scripts/deleted.js", "export const value = 1;\n"}};
    engine::AssetSyncSystem sync;
    if (!check(!sync.sync(source, runtime), "uninitialized sync rejected") ||
        !check(sync.initialize({}), "initialize sync") ||
        !check(write_file(source / "scripts/game.js", scripts.at("scripts/game.js")) &&
                   !sync.sync(source, runtime),
               "loose script tree without catalog rejected") ||
        !check(publish_scripts(source, scripts), "write unified source catalog") ||
        !check(sync.sync(source, runtime), "publish initial version"))
    {
        return false;
    }
    const auto first = read_json(available);
    const auto first_revision = sync.revision();
    std::error_code error;
    const auto first_timestamp = fs::last_write_time(available, error);
    if (!check(!error && first.is_object(), "read complete manifest") ||
        !check(sync.sync(source, runtime), "repeat identical sync") ||
        !check(sync.revision() == first_revision, "identical content retains revision") ||
        !check(fs::last_write_time(available, error) == first_timestamp,
               "identical content does not republish manifest") ||
        !check(!sync.sync(source, runtime, "../escape.js"), "entry traversal rejected") ||
        !check(!sync.sync(source, source), "source/runtime overlap rejected") ||
        !check(read_json(available) == first, "failed sync keeps old manifest"))
    {
        return false;
    }
    scripts.erase("scripts/deleted.js");
    scripts["scripts/game.js"] = "export default class Game { updated = true; }\n";
    if (!check(publish_scripts(source, scripts) && sync.sync(source, runtime),
               "publish changed catalog and removed script"))
    {
        return false;
    }
    const auto second = read_json(available);
    if (!check(sync.revision() != first_revision, "changed content gets new revision") ||
        !check(second["deleted"] == Json::array({deleted_file}), "GUID deletion recorded") ||
        !check(fs::exists(runtime / "assets/versions" / first_revision / deleted_file, error),
               "old active snapshot remains intact"))
    {
        return false;
    }
    scripts["scripts/added.js"] = "export const added = 1;\n";
    if (!check(publish_scripts(source, scripts) && sync.sync(source, runtime),
               "publish added script"))
    {
        return false;
    }
    const auto third = read_json(available);
    if (!check(third["deleted"] == second["deleted"], "deletions persist across later syncs") ||
        !write_file(runtime / "assets/.staging-interrupted/catalog.bin", "bad"))
    {
        return false;
    }
    fs::create_directory(runtime / "assets/.sync-lock", error);
    if (!check(!error && !sync.sync(source, runtime),
               "concurrent or interrupted sync lock rejected") ||
        !check(read_json(available) == third, "interrupted transfer never changes manifest"))
    {
        return false;
    }
    fs::remove(runtime / "assets/.sync-lock", error);
    if (!check(!error && sync.sync(source, runtime), "retry after abandoned lock cleanup"))
    {
        return false;
    }
    // 指向已有实体的链接也不能作为部署来源。
    const auto outside = root / "external-entity";
    fs::copy_file(source / game_file, outside, error);
    if (error || !fs::remove(source / game_file, error) || error)
    {
        return false;
    }
    fs::create_symlink(outside, source / game_file, error);
    if (!error)
    {
        if (!check(!sync.sync(source, runtime), "referenced source symlink rejected") ||
            !check(read_json(available) == third, "symlink failure keeps old manifest"))
        {
            return false;
        }
        fs::remove(source / game_file, error);
        fs::create_directory_symlink(runtime, root / "redirected-runtime", error);
        if (!check(!error && !sync.sync(source, root / "redirected-runtime"),
                   "runtime symlink rejected"))
        {
            return false;
        }
    }
    else
    {
        std::fputs("Symlink checks skipped: host does not permit creating test symlinks\n", stderr);
    }
    if (!publish_scripts(source, scripts))
    {
        return false;
    }
    const auto current_revision = third["revision"].get<std::string>();
    if (!check(write_file(runtime / "assets/versions" / current_revision / game_file, "corrupt"),
               "simulate corrupted immutable version") ||
        !check(!sync.sync(source, runtime), "corrupted immutable version is rejected") ||
        !check(read_json(available) == third, "corruption does not replace available pointer"))
    {
        return false;
    }
    for (const char* invalid :
         {R"({"files":[false],"deleted":[]})", R"({"files":[],"deleted":[null]})",
          R"({"files":[{"path":"../outside"}],"deleted":[]})"})
    {
        if (!check(write_file(available, invalid), "write invalid external manifest") ||
            !check(!sync.sync(source, runtime), "invalid manifest rejected without exceptions"))
        {
            return false;
        }
    }
    return true;
}

bool guid_exercise(const fs::path& root)
{
    const auto source = root / "guid-cache";
    const auto runtime = root / "guid-runtime";
    const std::string guid = "20000000-0000-4000-8000-000000000001";
    const std::string script = "export default class Game {}\n";
    engine::ByteBuffer entity;
    std::uint32_t header_size = 0;
    if (!engine::ResourceSystem::encode_resource(
            guid, engine::schema::AssetKind::script,
            engine::ByteView(reinterpret_cast<const std::byte*>(script.data()), script.size()),
            entity, header_size))
    {
        return false;
    }
    const std::string encoded_entity(reinterpret_cast<const char*>(entity.data()), entity.size());
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char value : encoded_entity)
    {
        hash = (hash ^ value) * 1099511628211ULL;
    }
    std::array<char, 17> encoded{};
    std::snprintf(encoded.data(), encoded.size(), "%016llx", static_cast<unsigned long long>(hash));
    engine::schema::AssetCatalogDataT catalog;
    catalog.schema_version = 2;
    catalog.revision = "guid-sync-test";
    catalog.target_platform = "android";
    auto asset = std::make_unique<engine::schema::AssetEntryT>();
    asset->guid = guid;
    asset->path = "project/scripts/game.ts";
    asset->module_path = "scripts/game.js";
    asset->kind = engine::schema::AssetKind::script;
    asset->content_hash = encoded.data();
    asset->content_size = entity.size();
    asset->header_size = header_size;
    asset->location = std::make_unique<engine::schema::FileLocationT>();
    asset->location->mount = "content";
    asset->location->path = "gen/" + guid;
    asset->location->length = entity.size();
    catalog.entries.push_back(std::move(asset));
    flatbuffers::FlatBufferBuilder builder;
    engine::schema::FinishAssetCatalogDataBuffer(
        builder, engine::schema::AssetCatalogData::Pack(builder, &catalog));
    if (!write_file(source / "catalog.bin",
                    std::string_view(reinterpret_cast<const char*>(builder.GetBufferPointer()),
                                     builder.GetSize())) ||
        !write_file(source / "gen" / guid, encoded_entity) ||
        !write_file(source / "tasks.sqlite", "editor-only database") ||
        !write_file(source / "gen/stale-artifact", "unreferenced generated output"))
    {
        return false;
    }
    engine::AssetSyncSystem sync;
    if (!check(sync.initialize({}) && sync.sync(source, runtime), "publish complete GUID catalog"))
    {
        return false;
    }
    const auto available = runtime / "assets/available.json";
    const auto first = read_json(available);
    if (!check(first["files"].size() == 2 && first["files"][0]["path"] == "catalog.bin" &&
                   first["files"][1]["path"] == "gen/" + guid,
               "only catalog and referenced GUID artifacts enter device snapshot") ||
        !write_file(source / "gen" / guid, "corrupt") ||
        !check(!sync.sync(source, runtime) && read_json(available) == first,
               "catalog hash mismatch preserves previous publication"))
    {
        return false;
    }
    // 合并包只同步一次物理文件，条目按清单范围寻址。
    auto& packed = *catalog.entries.front();
    packed.location->path = "packages/main";
    packed.location->offset = 9;
    catalog.revision = "packed-sync-test";
    builder.Clear();
    engine::schema::FinishAssetCatalogDataBuffer(
        builder, engine::schema::AssetCatalogData::Pack(builder, &catalog));
    if (!write_file(source / "packages/main", "pack-head" + encoded_entity + "pack-tail") ||
        !write_file(source / "catalog.bin",
                    std::string_view(reinterpret_cast<const char*>(builder.GetBufferPointer()),
                                     builder.GetSize())) ||
        !check(sync.sync(source, runtime), "package members synchronize through locations"))
    {
        return false;
    }
    const auto packed_manifest = read_json(available);
    if (!check(packed_manifest["files"].size() == 2 &&
                   packed_manifest["files"][1]["path"] == "packages/main",
               "only the referenced package and catalog are synchronized"))
    {
        return false;
    }
    // 包的物理尺寸可超过单实体上限；稀疏尾部不改变有效资源的定位。
    std::error_code error;
    fs::resize_file(source / "packages/main", 65ULL * 1024 * 1024, error);
    if (!check(!error && sync.sync(source, runtime), "large carrier hashes in bounded chunks"))
    {
        return false;
    }
    const auto large_manifest = read_json(available);
    if (!check(large_manifest["files"][1]["size"] == 65ULL * 1024 * 1024,
               "large carrier length is preserved in the deployment manifest"))
    {
        return false;
    }
    return true;
}

} // namespace

int main()
{
    std::error_code error;
    const auto temporary = fs::canonical(fs::temp_directory_path(error), error);
    if (error)
    {
        return 1;
    }
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = temporary / ("engine-asset-sync-test-" + std::to_string(nonce));
    const bool success = exercise(root) && guid_exercise(root);
    fs::remove_all(root, error);
    return success && !error ? 0 : 1;
}
