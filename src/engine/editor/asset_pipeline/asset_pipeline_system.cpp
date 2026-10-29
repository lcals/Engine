#include "editor/asset_pipeline/asset_pipeline_system.hpp"

#include "editor/asset_pipeline/asset_database.hpp"
#include "editor/asset_pipeline/asset_importer_registry.hpp"
#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/asset_metadata.hpp"
#include "editor/asset_pipeline/asset_publish.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "editor/asset_pipeline/project_lock.hpp"
#include "editor/task/task_system.hpp"
#include "runtime/resource/builtin_resources.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/asset_catalog_generated.h"
#include "runtime/utils/path_utils.hpp"

#include <efsw/efsw.hpp>
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>
#include <meshoptimizer.h>
#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace engine::editor
{
namespace
{

using namespace asset_pipeline;

constexpr std::string_view importer_version = "engine-asset-importers-7";

bool directory(const std::filesystem::path& path, std::string& error)
{
    std::error_code code;
    const auto status = std::filesystem::symlink_status(path, code);
    if (code || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status))
    {
        error = "required project directory is missing, inaccessible or a symlink: " +
                PathUtils::to_utf8(path);
        return false;
    }
    return true;
}

bool reject_symlink_if_present(const std::filesystem::path& path, std::string& error)
{
    std::error_code code;
    const auto status = std::filesystem::symlink_status(path, code);
    if (code == std::errc::no_such_file_or_directory)
    {
        return true;
    }
    if (code || std::filesystem::is_symlink(status))
    {
        error = "cache path is inaccessible or a symlink: " + PathUtils::to_utf8(path);
        return false;
    }
    return true;
}

bool project_idle(const std::filesystem::path& root, std::string& error)
{
    const auto hosts = root / "cache/editor-hosts";
    std::error_code code;
    if (!std::filesystem::exists(hosts, code))
    {
        if (code)
        {
            error = "cannot inspect editor resource ownership: " + code.message();
        }
        return !code;
    }
    if (!directory(hosts, error))
    {
        return false;
    }
    const auto canonical_root = std::filesystem::weakly_canonical(root, code);
    if (code)
    {
        error = "cannot canonicalize editor project identity";
        return false;
    }
    std::filesystem::directory_iterator iterator(hosts, code);
    const std::filesystem::directory_iterator end;
    while (!code && iterator != end)
    {
        const auto marker = iterator->path();
        if (!reject_symlink_if_present(marker, error))
        {
            return false;
        }
        if (marker.extension() == ".json")
        {
            auto lease_path = marker;
            lease_path.replace_extension(".lock");
            ProjectLock lease;
            bool acquired = false;
            if (!reject_symlink_if_present(lease_path, error) ||
                !lease.try_acquire(lease_path, acquired, error))
            {
                error = "cannot inspect editor host lease: " + error;
                return false;
            }
            if (acquired)
            {
                // OS 释放已退出进程的锁；遗留登记不代表仍持有资源的宿主。
                iterator.increment(code);
                continue;
            }
            std::string marker_text;
            if (!ResourceSystem::read_text(marker, marker_text))
            {
                error = "cannot read editor ownership marker: " + PathUtils::to_utf8(marker);
                return false;
            }
            const auto host = nlohmann::json::parse(marker_text, nullptr, false);
            if (!host.is_object() || !host.contains("projectRoot") ||
                !host["projectRoot"].is_string() || !host.contains("runtimeDir") ||
                !host["runtimeDir"].is_string())
            {
                error = "invalid editor ownership marker: " + PathUtils::to_utf8(marker);
                return false;
            }
            const auto registered_root = std::filesystem::weakly_canonical(
                PathUtils::from_utf8(host["projectRoot"].get<std::string>()), code);
            if (code || registered_root != canonical_root)
            {
                error = "editor ownership marker has a mismatched project identity";
                return false;
            }
            std::string status_text;
            const auto runtime = PathUtils::from_utf8(host["runtimeDir"].get<std::string>());
            nlohmann::json status;
            if (runtime.is_absolute() &&
                ResourceSystem::read_text(runtime / "project-status.json", status_text))
            {
                status = nlohmann::json::parse(status_text, nullptr, false);
            }
            if (!status.is_object() || !status.contains("projectState") ||
                !status["projectState"].is_string())
            {
                error = "editor is initializing or its live project status cannot be read; "
                        "retry after it is idle";
                return false;
            }
            if (!status.contains("projectRoot") || !status["projectRoot"].is_string() ||
                !status.contains("session") || !status["session"].is_string() ||
                status["session"].get<std::string>() != PathUtils::to_utf8(marker.stem()))
            {
                error = "editor project status has a missing or mismatched session identity";
                return false;
            }
            const auto status_root = std::filesystem::weakly_canonical(
                PathUtils::from_utf8(status["projectRoot"].get<std::string>()), code);
            if (code || status_root != canonical_root)
            {
                error = "editor project status has a mismatched project identity";
                return false;
            }
            const auto state = status["projectState"].get<std::string>();
            if (state == "running" || state == "starting" || state == "stopping")
            {
                error = "Stop the active editor project before rebuilding its resource cache";
                return false;
            }
            if (state != "idle" && state != "closed")
            {
                error = "editor project status does not confirm that the cache is idle";
                return false;
            }
        }
        iterator.increment(code);
    }
    if (code)
    {
        error = "cannot enumerate editor ownership markers: " + code.message();
        return false;
    }
    return true;
}

bool import_threads(const std::filesystem::path& settings, std::size_t& count, std::string& error)
{
    count = 4;
    const auto path = settings / "import.toml";
    std::error_code code;
    const bool exists = std::filesystem::exists(path, code);
    if (code)
    {
        error = "cannot inspect settings/import.toml";
        return false;
    }
    if (!exists)
    {
        return true;
    }
    std::string text;
    if (!ResourceSystem::read_text(path, text))
    {
        error = "cannot read settings/import.toml";
        return false;
    }
    auto parsed = toml::parse(text);
    if (!parsed)
    {
        error = "invalid settings/import.toml: " + std::string(parsed.error().description());
        return false;
    }
    if (!parsed.table().contains("import_threads"))
    {
        return true;
    }
    const auto value = parsed["import_threads"].value<std::int64_t>();
    if (!value || *value < 1 || *value > 64)
    {
        error = "settings/import.toml import_threads must be an integer from 1 to 64";
        return false;
    }
    count = static_cast<std::size_t>(*value);
    return true;
}

bool target_settings(std::string_view requested, std::string& platform, std::string& error)
{
    platform = requested;
    if (platform.empty())
    {
#if defined(__APPLE__)
        platform = "macos";
#elif defined(_WIN32)
        platform = "windows";
#elif defined(__ANDROID__)
        platform = "android";
#else
        platform = "linux";
#endif
    }
    if (platform != "macos" && platform != "ios" && platform != "windows" &&
        platform != "android" && platform != "linux")
    {
        error = "unsupported asset target platform: " + platform;
        return false;
    }
    return true;
}

bool builtin_identities(GuidMap& identities, std::map<Guid, schema::AssetKind>& kinds)
{
    for (const auto& entry : builtin_resource_catalog().entries)
    {
        if (!entry || !identities.add(entry->path, entry->guid))
        {
            return false;
        }
        kinds.emplace(entry->guid, entry->kind);
    }
    return true;
}

bool remembered_paths(const std::filesystem::path& cache,
                      const std::map<Guid, AssetRecord>& previous, std::set<std::string>& paths,
                      std::string& error)
{
    for (const auto& [guid, record] : previous)
    {
        (void)guid;
        paths.insert(record.asset.path);
    }
    std::error_code code;
    if (!std::filesystem::exists(cache / "catalog.bin", code))
    {
        if (code)
        {
            error = "cannot inspect previous catalog";
            return false;
        }
        return true;
    }
    std::vector<std::byte> bytes;
    if (!read_bytes(cache / "catalog.bin", bytes, error))
    {
        return false;
    }
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size());
    if (!schema::VerifyAssetCatalogDataBuffer(verifier))
    {
        // 已有 SQLite 保留身份时允许重建损坏的目录。
        if (!previous.empty())
        {
            return true;
        }
        error = "previous catalog is damaged and no database identity record is available";
        return false;
    }
    const auto* catalog = schema::GetAssetCatalogData(bytes.data());
    for (const auto* entry : *catalog->entries())
    {
        paths.insert(entry->path()->str());
    }
    return true;
}

bool scan_assets(const std::filesystem::path& assets, const std::filesystem::path& cache,
                 const std::set<std::string>& remembered, const AssetImporterRegistry& importers,
                 GuidMap& identities, std::map<Guid, schema::AssetKind>& kinds,
                 std::vector<AssetRecord>& records, std::string& error)
{
    std::error_code code;
    std::vector<std::filesystem::path> files;
    std::filesystem::recursive_directory_iterator iterator(assets, code);
    const std::filesystem::recursive_directory_iterator end;
    while (!code && iterator != end)
    {
        const auto status = iterator->symlink_status(code);
        if (code || std::filesystem::is_symlink(status))
        {
            error = "asset symlinks and inaccessible entries are not supported: " +
                    PathUtils::to_utf8(iterator->path());
            return false;
        }
        if (std::filesystem::is_regular_file(status))
        {
            const auto name = PathUtils::to_utf8(iterator->path().filename());
            if (!name.ends_with(".meta") && name != ".gitkeep")
            {
                files.push_back(iterator->path());
            }
        }
        else if (!std::filesystem::is_directory(status))
        {
            error = "unsupported non-file asset: " + PathUtils::to_utf8(iterator->path());
            return false;
        }
        iterator.increment(code);
    }
    if (code)
    {
        error = "cannot scan project assets: " + code.message();
        return false;
    }
    std::sort(files.begin(), files.end());
    std::set<Guid> reserved;
    for (const auto& [guid, kind] : kinds)
    {
        (void)kind;
        reserved.insert(guid);
    }
    for (const auto& source : files)
    {
        AssetRecord record;
        record.asset.source = source;
        record.asset.path = "project/" + generic_path(source.lexically_relative(assets));
        const auto* importer = importers.select(source);
        if (importer == nullptr)
        {
            error = "missing or ambiguous resource importer: " + record.asset.path;
            return false;
        }
        record.asset.kind = importer->kind();
        record.asset.importer_id = importer->id();
        record.module_path = importer->module_path(source.lexically_relative(assets));
        std::string meta_text;
        const auto defaults = importer->default_meta();
        if (!read_or_create_asset_meta(source, cache / ".asset-meta", record.asset.importer_id,
                                       defaults, !remembered.contains(record.asset.path), meta_text,
                                       record.asset.guid, error))
        {
            return false;
        }
        if (!reserved.insert(record.asset.guid).second ||
            !identities.add(record.asset.path, record.asset.guid) ||
            !kinds.emplace(record.asset.guid, record.asset.kind).second)
        {
            error = "invalid/duplicate resource GUID or logical path: " + record.asset.path + " (" +
                    record.asset.guid + ")";
            return false;
        }
        std::vector<ImportSubresource> descriptions;
        std::vector<ImportAsset> children;
        if (!importer->discover(record.asset, descriptions, error) ||
            !update_subresource_meta(record.asset, cache / ".asset-meta", descriptions, remembered,
                                     meta_text, children, error))
        {
            return false;
        }
        std::set<std::string> active;
        for (const auto& description : descriptions)
        {
            active.insert(description.key);
        }
        record.meta_hash = hash_text(meta_text);
        if (!file_hash(source, record.source_hash, error))
        {
            return false;
        }
        for (auto& child : children)
        {
            if (!reserved.insert(child.guid).second)
            {
                error = "Duplicate active or dormant subresource GUID: " + child.path;
                return false;
            }
            if (!active.contains(child.subresource_key))
            {
                continue;
            }
            if (!identities.add(child.path, child.guid) ||
                !kinds.emplace(child.guid, child.kind).second)
            {
                error = "Duplicate subresource logical path: " + child.path;
                return false;
            }
            AssetRecord output;
            output.asset = std::move(child);
            output.source_hash = record.source_hash;
            output.meta_hash = record.meta_hash;
            records.push_back(std::move(output));
        }
        records.push_back(std::move(record));
    }
    return true;
}

bool common_fingerprint(const std::string& platform, std::string& output, std::string& error)
{
    std::string schemas;
    if (!schema_fingerprint({"common.fbs", "asset_catalog.fbs", "resource_header.fbs"}, schemas,
                            error))
    {
        return false;
    }
    output = std::string(importer_version) + ":" + platform + schemas;
    return true;
}

bool inspect_importers(const AssetImporterRegistry& importers, const ImportContext& context,
                       const std::vector<AssetRecord>& records,
                       std::map<std::string, std::string>& fingerprints, std::string& error)
{
    fingerprints.clear();
    for (const auto& record : records)
    {
        if (fingerprints.contains(record.asset.importer_id))
        {
            continue;
        }
        auto* importer = importers.find(record.asset.importer_id);
        std::string fingerprint;
        if (importer == nullptr || !importer->inspect_batch(context, fingerprint, error))
        {
            if (error.empty())
            {
                error = "cannot inspect importer: " + record.asset.path;
            }
            fingerprints.clear();
            return false;
        }
        fingerprints.emplace(record.asset.importer_id, std::move(fingerprint));
    }
    return true;
}

bool calculate_keys(std::vector<AssetRecord>& records, const std::string& common,
                    const std::map<std::string, std::string>& fingerprints,
                    std::map<Guid, std::size_t>& by_guid, std::string& error)
{
    std::vector<std::size_t> incoming(records.size(), 0);
    std::vector<std::vector<std::size_t>> dependents(records.size());
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        by_guid.emplace(records[index].asset.guid, index);
    }
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        for (const auto& guid : records[index].asset.build_dependencies)
        {
            const auto found = by_guid.find(guid);
            if (found != by_guid.end())
            {
                ++incoming[index];
                dependents[found->second].push_back(index);
            }
        }
    }
    std::queue<std::size_t> pending;
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        if (incoming[index] == 0)
        {
            pending.push(index);
        }
    }
    std::size_t visited = 0;
    while (!pending.empty())
    {
        const auto index = pending.front();
        pending.pop();
        ++visited;
        auto& record = records[index];
        std::string key = common + ":" + record.asset.importer_id + ":" + record.asset.path + ":" +
                          record.source_hash + ":" + record.meta_hash;
        const auto fingerprint = fingerprints.find(record.asset.importer_id);
        if (fingerprint == fingerprints.end())
        {
            error = "missing importer fingerprint: " + record.asset.path;
            return false;
        }
        key += ":" + fingerprint->second;
        for (const auto& guid : record.asset.build_dependencies)
        {
            const auto found = by_guid.find(guid);
            key += ":" + guid + ":" +
                   (found != by_guid.end() ? records[found->second].build_key : "builtin-1");
        }
        record.build_key = hash_text(key);
        for (const auto dependent : dependents[index])
        {
            if (--incoming[dependent] == 0)
            {
                pending.push(dependent);
            }
        }
    }
    if (visited != records.size())
    {
        error = "resource dependency cycle";
        return false;
    }
    return true;
}

bool verify_sources(const std::vector<AssetRecord>& records, std::string& error)
{
    for (const auto& record : records)
    {
        std::string hash;
        auto meta = record.asset.source;
        meta += ".meta";
        if (!file_hash(record.asset.source, hash, error) || hash != record.source_hash ||
            !file_hash(meta, hash, error) || hash != record.meta_hash)
        {
            error = "source or meta changed during import; retry the build: " + record.asset.path;
            return false;
        }
    }
    return true;
}

} // namespace

AssetPipelineSystem::AssetPipelineSystem()
{
    std::unique_ptr<System> subsystem = std::make_unique<TaskSystem>();
    auto* tasks = static_cast<TaskSystem*>(subsystem.get());
    if (add_subsystem(subsystem))
    {
        tasks_ = tasks;
    }
}

AssetPipelineSystem::~AssetPipelineSystem() noexcept
{
    shutdown();
}

bool AssetPipelineSystem::on_initialize(const EngineConfig&)
{
    if (tasks_ == nullptr || !register_builtin_render_resources())
    {
        return false;
    }
    efsw::FileWatcher watcher(true);
    constexpr float vertices[][3] = {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}};
    unsigned int remap[2]{};
    if (meshopt_generateVertexRemap(remap, nullptr, 2, vertices, 2, sizeof(vertices[0])) != 1)
    {
        return false;
    }
    return importers_.initialize();
}

void AssetPipelineSystem::on_shutdown() noexcept
{
    importers_.clear();
    catalog_ = {};
}

bool AssetPipelineSystem::add_importer(std::unique_ptr<asset_pipeline::AssetImporter>& importer)
{
    std::unique_lock lock(build_mutex_, std::try_to_lock);
    if (!lock.owns_lock())
    {
        return false;
    }
    last_error_.clear();
    if (!is_initialized() || !importers_.add(importer))
    {
        last_error_ = "importer registration requires an initialized pipeline and unique id";
        return false;
    }
    return true;
}

bool AssetPipelineSystem::set_importer(std::unique_ptr<asset_pipeline::AssetImporter>& importer)
{
    std::unique_lock lock(build_mutex_, std::try_to_lock);
    if (!lock.owns_lock())
    {
        return false;
    }
    last_error_.clear();
    if (!is_initialized() || !importers_.replace(importer))
    {
        last_error_ = "importer replacement requires an initialized pipeline and registered id";
        return false;
    }
    return true;
}

bool AssetPipelineSystem::build_scripts(const std::filesystem::path& project_root)
{
    return build_assets(project_root);
}

bool AssetPipelineSystem::build_assets(const std::filesystem::path& project_root,
                                       std::string_view requested_target)
{
    std::unique_lock build_lock(build_mutex_, std::try_to_lock);
    if (!build_lock.owns_lock())
    {
        return false;
    }
    last_error_.clear();
    if (!is_initialized() || tasks_ == nullptr)
    {
        last_error_ = "asset pipeline is not initialized";
        return false;
    }
    std::error_code code;
    const auto root = std::filesystem::absolute(project_root, code).lexically_normal();
    if (code || !directory(root, last_error_))
    {
        if (last_error_.empty())
        {
            last_error_ = "cannot resolve project path";
        }
        return false;
    }
    for (const auto name : {"settings", "assets", "cache"})
    {
        if (!directory(root / name, last_error_))
        {
            return false;
        }
    }
    const auto cache = root / "cache";
    for (const auto name : {"assets.sqlite", "assets.sqlite-journal", "gen", "catalog.bin",
                            ".asset-publish", ".asset-meta", ".asset-build.lock"})
    {
        if (!reject_symlink_if_present(cache / name, last_error_))
        {
            return false;
        }
    }
    ProjectLock lock;
    if (!lock.acquire(cache / ".asset-build.lock", last_error_))
    {
        return false;
    }
    if (!project_idle(root, last_error_))
    {
        return false;
    }
    AssetDatabase database;
    if (!database.open(cache / "assets.sqlite", last_error_) ||
        !recover_publish(cache, database, last_error_))
    {
        return false;
    }
    std::map<Guid, AssetRecord> previous;
    std::set<std::string> remembered;
    if (!database.load(previous, last_error_) ||
        !remembered_paths(cache, previous, remembered, last_error_))
    {
        return false;
    }

    AssetBuildStats stats;
    if (!import_threads(root / "settings", stats.import_threads, last_error_) ||
        !tasks_->configure_threads(stats.import_threads))
    {
        if (last_error_.empty())
        {
            last_error_ = "cannot configure import workers while tasks are running";
        }
        return false;
    }
    std::string platform;
    if (!target_settings(requested_target, platform, last_error_))
    {
        return false;
    }
    GuidMap identities;
    std::map<Guid, schema::AssetKind> kinds;
    if (!builtin_identities(identities, kinds))
    {
        last_error_ = "cannot register builtin resource identities";
        return false;
    }
    // meta 先在缓存临时区完整写入，避免中断时在 assets 留下会被误导入的半成品。
    std::filesystem::remove_all(cache / ".asset-meta", code);
    if (!code)
    {
        std::filesystem::create_directory(cache / ".asset-meta", code);
    }
    if (code)
    {
        last_error_ = "cannot prepare meta staging: " + code.message();
        return false;
    }
    const auto& importers = importers_;
    std::vector<AssetRecord> records;
    const bool scanned = scan_assets(root / "assets", cache, remembered, importers, identities,
                                     kinds, records, last_error_);
    std::filesystem::remove_all(cache / ".asset-meta", code);
    if (!scanned || code)
    {
        if (last_error_.empty())
        {
            last_error_ = "cannot clean meta staging: " + code.message();
        }
        return false;
    }
    stats.scanned = records.size();
    const auto workspace = cache / ".asset-publish";
    const auto output = workspace / "staged-gen";
    schema::AssetCatalogDataT previous_catalog;
    std::vector<std::byte> previous_bytes;
    if (ResourceSystem::read_binary(cache / "catalog.bin", previous_bytes))
    {
        flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(previous_bytes.data()),
                                       previous_bytes.size());
        if (schema::VerifyAssetCatalogDataBuffer(verifier))
        {
            schema::GetAssetCatalogData(previous_bytes.data())->UnPackTo(&previous_catalog);
        }
    }
    std::map<Guid, const schema::AssetEntryT*> previous_entries;
    if (previous_catalog.schema_version == 2)
    {
        for (const auto& entry : previous_catalog.entries)
        {
            previous_entries.emplace(entry->guid, entry.get());
        }
    }
    std::vector<const ImportAsset*> sources;
    sources.reserve(records.size());
    for (const auto& record : records)
    {
        sources.push_back(&record.asset);
    }
    ImportContext context{identities,        kinds,  root, root / "assets", output, platform,
                          &previous_catalog, sources};
    std::string common_key;
    std::map<std::string, std::string> fingerprints;
    if (!common_fingerprint(platform, common_key, last_error_) ||
        !inspect_importers(importers, context, records, fingerprints, last_error_))
    {
        return false;
    }
    for (auto& record : records)
    {
        const auto* importer = importers.find(record.asset.importer_id);
        if (importer == nullptr || !importer->scan_dependencies(record.asset, context, last_error_))
        {
            last_error_ = record.asset.path + ": " + last_error_;
            return false;
        }
    }
    std::map<Guid, std::size_t> by_guid;
    if (!calculate_keys(records, common_key, fingerprints, by_guid, last_error_))
    {
        return false;
    }
    for (const auto& [guid, record] : previous)
    {
        (void)record;
        if (!by_guid.contains(guid))
        {
            ++stats.removed;
        }
    }
    std::filesystem::create_directories(output, code);
    if (code)
    {
        last_error_ = "cannot create asset staging directory: " + code.message();
        return false;
    }
    const auto fail = [&]()
    {
        std::string cleanup_error;
        if (!recover_publish(cache, database, cleanup_error))
        {
            last_error_ += "; " + cleanup_error;
        }
        return false;
    };
    std::vector<bool> changed(records.size(), true);
    std::set<std::string> changed_types;
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        auto& record = records[index];
        const auto old = previous.find(record.asset.guid);
        const auto old_entry = previous_entries.find(record.asset.guid);
        if (old != previous.end() && old->second.build_key == record.build_key &&
            old_entry != previous_entries.end())
        {
            std::vector<std::byte> bytes;
            std::vector<std::byte> reused_payload;
            const auto file = cache / "gen" / record.asset.guid;
            const auto status = std::filesystem::symlink_status(file, code);
            if (!code && std::filesystem::is_regular_file(status) &&
                !std::filesystem::is_symlink(status) && ResourceSystem::read_binary(file, bytes) &&
                old->second.output_size == bytes.size() &&
                old->second.output_hash == hash_bytes(bytes) &&
                ResourceSystem::decode_resource(*old_entry->second, bytes, reused_payload))
            {
                record.output_hash = old->second.output_hash;
                record.output_size = old->second.output_size;
                record.header_size = old_entry->second->header_size;
                std::filesystem::create_hard_link(file, output / record.asset.guid, code);
                if (code)
                {
                    code.clear();
                    std::filesystem::copy_file(file, output / record.asset.guid, code);
                }
                if (code)
                {
                    last_error_ = "cannot stage reusable resource: " + code.message();
                    return fail();
                }
                changed[index] = false;
                ++stats.reused;
            }
            code.clear();
        }
        if (changed[index])
        {
            ++stats.imported;
            changed_types.insert(record.asset.importer_id);
        }
    }
    std::vector<TaskDescription> tasks;
    std::vector<std::string> errors(records.size());
    std::map<std::string, std::string> batch_errors;
    std::map<std::string, std::size_t> batch_tasks;
    for (const auto& kind : changed_types)
    {
        TaskDescription task;
        const auto* importer = importers.find(kind);
        auto& error = batch_errors[kind];
        if (importer == nullptr || !importer->prepare_batch(context, task, error))
        {
            last_error_ = error.empty() ? "cannot prepare importer batch" : error;
            return fail();
        }
        if (task.execute)
        {
            batch_tasks.emplace(kind, tasks.size());
            tasks.push_back(std::move(task));
        }
    }
    std::map<Guid, std::size_t> task_by_guid;
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        if (!changed[index])
        {
            continue;
        }
        task_by_guid.emplace(records[index].asset.guid, tasks.size());
        tasks.push_back({records[index].asset.path,
                         [&, index]()
                         {
                             auto& record = records[index];
                             const auto* importer = importers.find(record.asset.importer_id);
                             if (importer == nullptr ||
                                 !importer->convert(record.asset, context, errors[index]))
                             {
                                 return false;
                             }
                             std::vector<std::byte> bytes;
                             if (!read_bytes(output / record.asset.guid, bytes, errors[index]))
                             {
                                 return false;
                             }
                             record.output_size = bytes.size();
                             record.output_hash = hash_bytes(bytes);
                             return true;
                         },
                         {}});
    }
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        if (!changed[index])
        {
            continue;
        }
        auto& task = tasks[task_by_guid.at(records[index].asset.guid)];
        const auto batch = batch_tasks.find(records[index].asset.importer_id);
        if (batch != batch_tasks.end())
        {
            task.dependencies.push_back(batch->second);
        }
        for (const auto& guid : records[index].asset.build_dependencies)
        {
            const auto dependency = task_by_guid.find(guid);
            if (dependency != task_by_guid.end())
            {
                task.dependencies.push_back(dependency->second);
            }
        }
    }
    if (!tasks_->run(tasks))
    {
        last_error_ = tasks_->last_error();
        for (const auto& error : errors)
        {
            if (!error.empty())
            {
                last_error_ += "\n" + error;
            }
        }
        for (const auto& [kind, error] : batch_errors)
        {
            (void)kind;
            if (!error.empty())
            {
                last_error_ += "\n" + error;
            }
        }
        return fail();
    }
    // 转换器读取暂存的原始载荷；所有依赖转换结束后再统一封装资源实体。
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        if (!changed[index])
        {
            continue;
        }
        auto& record = records[index];
        std::vector<std::byte> payload;
        std::vector<std::byte> entity;
        if (!read_bytes(output / record.asset.guid, payload, last_error_) ||
            !ResourceSystem::encode_resource(record.asset.guid, record.asset.kind, payload, entity,
                                             record.header_size) ||
            !write_bytes(output / record.asset.guid, entity, last_error_))
        {
            if (last_error_.empty())
            {
                last_error_ = "cannot encode resource entity: " + record.asset.path;
            }
            return fail();
        }
        record.output_size = entity.size();
        record.output_hash = hash_bytes(entity);
    }
    if (!verify_sources(records, last_error_))
    {
        return fail();
    }
    // 类型工具与输入快照同样需要稳定，防止发布混合产物。
    std::string rechecked_common;
    std::map<std::string, std::string> rechecked_fingerprints;
    if (!common_fingerprint(platform, rechecked_common, last_error_) ||
        !inspect_importers(importers, context, records, rechecked_fingerprints, last_error_) ||
        rechecked_common != common_key || rechecked_fingerprints != fingerprints)
    {
        if (last_error_.empty())
        {
            last_error_ = "import tools or settings changed during build; retry";
        }
        return fail();
    }
    // 发布前再检查暂存产物，缓存复用也不允许中途被外部写入改变。
    for (const auto& record : records)
    {
        std::vector<std::byte> bytes;
        if (!read_bytes(output / record.asset.guid, bytes, last_error_) ||
            bytes.size() != record.output_size || hash_bytes(bytes) != record.output_hash)
        {
            last_error_ = "staged resource changed during import: " + record.asset.path;
            return fail();
        }
    }
    std::string previous_revision;
    if (!database.revision(previous_revision, last_error_))
    {
        return fail();
    }
    schema::AssetCatalogDataT catalog;
    catalog.schema_version = 2;
    catalog.revision = previous_revision.empty() ? new_guid() : previous_revision;
    catalog.target_platform = platform;
    for (const auto& record : records)
    {
        auto entry = std::make_unique<schema::AssetEntryT>();
        entry->guid = record.asset.guid;
        entry->path = record.asset.path;
        entry->kind = record.asset.kind;
        entry->content_hash = record.output_hash;
        entry->content_size = record.output_size;
        entry->dependencies = record.asset.runtime_dependencies;
        entry->module_path = record.module_path;
        entry->header_size = record.header_size;
        entry->location = std::make_unique<schema::FileLocationT>();
        entry->location->mount = "content";
        entry->location->path = "gen/" + record.asset.guid;
        entry->location->length = record.output_size;
        catalog.entries.push_back(std::move(entry));
    }
    ResourceCatalog validated;
    if (!validated.assign(catalog))
    {
        last_error_ = "runtime resource catalog has invalid metadata, missing dependencies or a "
                      "dependency cycle";
        return fail();
    }
    // 没有变化时保持 revision/catalog/正式文件时间不变；同时确认目录没有多余文件。
    if (stats.imported == 0 && stats.removed == 0 && previous.size() == records.size() &&
        !previous_revision.empty())
    {
        bool exact_files = true;
        std::size_t file_count = 0;
        std::filesystem::directory_iterator generated(cache / "gen", code);
        const std::filesystem::directory_iterator end;
        while (!code && generated != end)
        {
            const auto filename = PathUtils::to_utf8(generated->path().filename());
            const auto status = generated->symlink_status(code);
            if (code || !std::filesystem::is_regular_file(status) ||
                std::filesystem::is_symlink(status) || !by_guid.contains(filename))
            {
                exact_files = false;
                break;
            }
            ++file_count;
            generated.increment(code);
        }
        exact_files = exact_files && !code && file_count == records.size();
        code.clear();
        std::vector<std::byte> catalog_bytes;
        if (exact_files && ResourceSystem::read_binary(cache / "catalog.bin", catalog_bytes))
        {
            flatbuffers::Verifier verifier(
                reinterpret_cast<const std::uint8_t*>(catalog_bytes.data()), catalog_bytes.size());
            if (schema::VerifyAssetCatalogDataBuffer(verifier))
            {
                std::unique_ptr<schema::AssetCatalogDataT> previous_catalog(
                    schema::GetAssetCatalogData(catalog_bytes.data())->UnPack());
                if (*previous_catalog == catalog)
                {
                    stats.revision = previous_revision;
                    if (!recover_publish(cache, database, last_error_))
                    {
                        return false;
                    }
                    last_build_ = std::move(stats);
                    catalog_ = std::move(catalog);
                    return true;
                }
            }
        }
    }
    stats.revision = new_guid();
    catalog.revision = stats.revision;
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishAssetCatalogDataBuffer(builder,
                                         schema::AssetCatalogData::Pack(builder, &catalog));
    if (!write_bytes(workspace / "staged-catalog.bin",
                     std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                     last_error_) ||
        !publish_assets(cache, database, records, stats.revision, last_error_))
    {
        return fail();
    }
    last_build_ = std::move(stats);
    catalog_ = std::move(catalog);
    return true;
}

const AssetBuildStats& AssetPipelineSystem::last_build() const noexcept
{
    return last_build_;
}

const std::string& AssetPipelineSystem::last_error() const noexcept
{
    return last_error_;
}

const schema::AssetCatalogDataT& AssetPipelineSystem::catalog() const noexcept
{
    return catalog_;
}

} // namespace engine::editor
