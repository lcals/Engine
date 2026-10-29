#include "plugins/editor/asset_build/asset_build_system.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "editor/asset_pipeline/project_lock.hpp"
#include "runtime/resource/builtin_resources.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <flatbuffers/flatbuffers.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace engine
{
namespace
{

using namespace asset_pipeline;

struct BuildItem
{
    std::string name;
    bool package = false;
    std::vector<std::string> roots;
    std::set<Guid> resources;
};

struct BuildManifest
{
    std::string platform;
    std::filesystem::path output;
    std::vector<BuildItem> items;
};

bool relative_path(const std::filesystem::path& path)
{
    const auto text = PathUtils::to_utf8(path);
    if (text.empty() || path.is_absolute() || path.has_root_path() ||
        text.find_first_of("\\:") != std::string::npos || text.find('\0') != std::string::npos)
    {
        return false;
    }
    for (const auto& part : path)
    {
        if (part.empty() || part == "." || part == "..")
        {
            return false;
        }
    }
    return path.lexically_normal() == path;
}

bool inspect_path(const std::filesystem::path& path, std::string& error)
{
    std::filesystem::path current;
    std::error_code code;
    for (const auto& part : path)
    {
        current /= part;
        const auto status = std::filesystem::symlink_status(current, code);
        if (code == std::errc::no_such_file_or_directory)
        {
            code.clear();
            continue;
        }
        if (code || std::filesystem::is_symlink(status))
        {
            error =
                "build path is inaccessible or contains a symlink: " + PathUtils::to_utf8(current);
            return false;
        }
    }
    return true;
}

bool item_name(std::string_view value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(),
                                         [](char ch)
                                         {
                                             return (ch >= 'a' && ch <= 'z') ||
                                                    (ch >= '0' && ch <= '9') || ch == '-' ||
                                                    ch == '_';
                                         });
}

bool read_paths(const toml::table& table, std::string_view key, bool required,
                std::vector<std::string>& output, std::string& error)
{
    const auto* node = table.get(key);
    if (node == nullptr && !required)
    {
        return true;
    }
    const auto* list = node != nullptr ? node->as_array() : nullptr;
    if (list == nullptr || (required && list->empty()))
    {
        error = "build item " + std::string(key) + " must be a nonempty path array";
        return false;
    }
    for (const auto& value : *list)
    {
        const auto path = value.value<std::string>();
        GuidMap identity;
        if (!path || !identity.add(*path, "00000000-0000-4000-8000-000000000001"))
        {
            error = "build entry must be a valid builtin/ or project/ logical path";
            return false;
        }
        output.push_back(*path);
    }
    return true;
}

bool read_manifest(const std::filesystem::path& root, const std::filesystem::path& path,
                   std::span<const std::string> selected, BuildManifest& output, std::string& error)
{
    if (!relative_path(path) || *path.begin() != "settings" || !inspect_path(root / path, error))
    {
        error = "build manifest must be a relative file inside project settings/";
        return false;
    }
    std::string text;
    if (!ResourceSystem::read_text(root / path, text))
    {
        error = "cannot read resource build manifest";
        return false;
    }
    auto parsed = toml::parse(text);
    if (!parsed)
    {
        error = "invalid resource build manifest: " + std::string(parsed.error().description());
        return false;
    }
    for (const auto& [key, value] : parsed.table())
    {
        (void)value;
        if (key != "target_platform" && key != "output" && key != "items")
        {
            error = "unknown resource build setting: " + std::string(key.str());
            return false;
        }
    }
    const auto target = parsed["target_platform"].value<std::string>();
    const auto destination = parsed["output"].value<std::string>();
    if (!target || !destination || destination->empty())
    {
        error = "build manifest requires target_platform and output strings";
        return false;
    }
    output.platform = *target;
    output.output = PathUtils::from_utf8(*destination);
    if (!relative_path(output.output) || !destination->starts_with("cache/build/") ||
        !inspect_path(root / output.output, error))
    {
        error = "build output must be a dedicated directory below cache/build/";
        return false;
    }
    std::set<std::string> selection;
    for (const auto& name : selected)
    {
        if (!item_name(name) || !selection.insert(name).second)
        {
            error = "invalid or duplicate build item selection: " + name;
            return false;
        }
    }
    const auto* items = parsed["items"].as_array();
    if (items == nullptr || items->empty())
    {
        error = "build manifest must define at least one [[items]] table";
        return false;
    }
    std::set<std::string> names;
    for (const auto& value : *items)
    {
        const auto* table = value.as_table();
        if (table == nullptr)
        {
            error = "build item must be a table";
            return false;
        }
        for (const auto& [key, field] : *table)
        {
            (void)field;
            if (key != "name" && key != "storage" && key != "roots" && key != "keep")
            {
                error = "unknown resource build item setting: " + std::string(key.str());
                return false;
            }
        }
        BuildItem item;
        item.name = (*table)["name"].value_or(std::string{});
        const auto storage = (*table)["storage"].value_or(std::string{});
        if (!item_name(item.name) || !names.insert(item.name).second ||
            (storage != "files" && storage != "package") ||
            !read_paths(*table, "roots", true, item.roots, error) ||
            !read_paths(*table, "keep", false, item.roots, error))
        {
            if (error.empty())
            {
                error = "build item requires a unique name and files/package storage";
            }
            return false;
        }
        item.package = storage == "package";
        if (selected.empty() || selection.contains(item.name))
        {
            output.items.push_back(std::move(item));
        }
    }
    for (const auto& name : selection)
    {
        if (!names.contains(name))
        {
            error = "unknown resource build item: " + name;
            return false;
        }
    }
    std::sort(output.items.begin(), output.items.end(),
              [](const auto& left, const auto& right) { return left.name < right.name; });
    return true;
}

bool collect(const ResourceCatalog& catalog, std::string_view guid, std::set<Guid>& visited,
             std::set<Guid>& resources, std::string& error)
{
    std::vector<Guid> pending{std::string(guid)};
    while (!pending.empty())
    {
        auto current = std::move(pending.back());
        pending.pop_back();
        if (!visited.insert(current).second)
        {
            continue;
        }
        const auto* entry = catalog.find(current);
        if (entry == nullptr)
        {
            entry = builtin_resource_info(current);
        }
        if (entry == nullptr)
        {
            error = "runtime dependency has no resource: " + current;
            return false;
        }
        if (entry->path.starts_with("project/"))
        {
            resources.insert(entry->guid);
        }
        pending.insert(pending.end(), entry->dependencies.begin(), entry->dependencies.end());
    }
    return true;
}

bool select_resources(const ResourceCatalog& catalog, BuildManifest& manifest, std::string& error)
{
    std::map<Guid, bool> storage;
    for (auto& item : manifest.items)
    {
        std::set<Guid> visited;
        std::set<Guid> closure;
        for (const auto& path : item.roots)
        {
            Guid guid;
            if (!catalog.identities().path_to_guid(path, guid))
            {
                for (const auto& builtin : builtin_resource_catalog().entries)
                {
                    if (builtin->path == path)
                    {
                        guid = builtin->guid;
                        break;
                    }
                }
            }
            if (guid.empty() || !collect(catalog, guid, visited, closure, error))
            {
                if (error.empty())
                {
                    error = "build entry is absent from the asset catalog: " + path;
                }
                return false;
            }
        }
        for (const auto& guid : closure)
        {
            const auto [found, inserted] = storage.emplace(guid, item.package);
            if (!inserted && found->second != item.package)
            {
                error = "resource is selected with conflicting files/package storage: " + guid;
                return false;
            }
            if (inserted)
            {
                item.resources.insert(guid);
            }
        }
    }
    return true;
}

bool copy_reusable(const std::filesystem::path& from, const std::filesystem::path& to,
                   std::string& error)
{
    std::error_code code;
    std::filesystem::create_hard_link(from, to, code);
    if (code)
    {
        code.clear();
        std::filesystem::copy_file(from, to, code);
    }
    if (code)
    {
        error = "cannot stage reusable build output: " + code.message();
        return false;
    }
    return true;
}

bool read_entity(const std::filesystem::path& cache, const schema::AssetEntryT& entry,
                 ByteBuffer& bytes, std::string& error)
{
    const auto file = cache / "gen" / entry.guid;
    ByteBuffer payload;
    if (!inspect_path(file, error) || !read_bytes(file, bytes, error) ||
        !ResourceSystem::decode_resource(entry, bytes, payload))
    {
        error = "imported resource entity is invalid: " + entry.path;
        return false;
    }
    return true;
}

bool verify_output(const ResourceCatalog& catalog, const std::filesystem::path& root,
                   const std::set<Guid>& resources)
{
    std::filesystem::path file;
    std::uint64_t expected_size = 0;
    for (const auto& guid : resources)
    {
        ByteBuffer payload;
        const auto* entry = catalog.find(guid);
        std::string error;
        if (entry == nullptr || !entry->location ||
            !inspect_path(root / PathUtils::from_utf8(entry->location->path), error) ||
            !ResourceSystem::read_resource(catalog, {{"content", root}}, guid, payload))
        {
            return false;
        }
        const auto current = root / PathUtils::from_utf8(entry->location->path);
        if (!file.empty() && file != current)
        {
            return false;
        }
        file = current;
        expected_size = std::max(expected_size, entry->location->offset + entry->location->length);
    }
    std::error_code code;
    return !file.empty() && std::filesystem::file_size(file, code) == expected_size && !code;
}

bool recover_output(const std::filesystem::path& output, const std::filesystem::path& backup,
                    std::string& error)
{
    std::error_code code;
    if (!std::filesystem::exists(backup, code))
    {
        if (code)
        {
            error = "cannot inspect previous resource build: " + code.message();
        }
        return !code;
    }
    const bool current = std::filesystem::exists(output, code);
    if (!code && !current)
    {
        std::filesystem::rename(backup, output, code);
    }
    else if (!code)
    {
        // 新目录只在完整暂存成功后换入；两者都存在表示交换已完成。
        std::filesystem::remove_all(backup, code);
    }
    if (code)
    {
        error = "cannot recover previous resource build: " + code.message();
        return false;
    }
    return true;
}

bool publish_output(const std::filesystem::path& staged, const std::filesystem::path& output,
                    const std::filesystem::path& backup, std::string& error)
{
    std::error_code code;
    const bool existed = std::filesystem::exists(output, code);
    if (!code && existed)
    {
        std::filesystem::rename(output, backup, code);
    }
    if (code)
    {
        error = "cannot retain previous resource build: " + code.message();
        return false;
    }
    std::filesystem::rename(staged, output, code);
    if (code)
    {
        error = "cannot publish resource build: " + code.message();
        if (existed)
        {
            std::filesystem::rename(backup, output, code);
            if (code)
            {
                error += "; previous output retained at " + PathUtils::to_utf8(backup);
            }
        }
        return false;
    }
    // 输出已成功发布；清理失败留待下次构建恢复。
    std::filesystem::remove_all(backup, code);
    return true;
}

} // namespace

AssetBuildSystem::AssetBuildSystem(AssetPipelineSystem& pipeline) noexcept : pipeline_(pipeline)
{
}

AssetBuildSystem::~AssetBuildSystem() noexcept
{
    shutdown();
}

bool AssetBuildSystem::on_initialize(const EngineConfig&)
{
    return pipeline_.is_initialized();
}

void AssetBuildSystem::on_shutdown() noexcept
{
}

bool AssetBuildSystem::build(const std::filesystem::path& project_root,
                             const std::filesystem::path& manifest_path,
                             std::span<const std::string> selected)
{
    last_error_.clear();
    if (!is_initialized() || !pipeline_.is_initialized() || building_)
    {
        last_error_ = "asset build system is not ready or a build is already running";
        return false;
    }
    building_ = true;
    struct BuildScope
    {
        bool& running;
        ~BuildScope() noexcept
        {
            running = false;
        }
    } scope{building_};
    std::error_code code;
    // 宿主的 /var 等标准路径别名先规范到真实项目根；项目子路径仍逐段拒绝符号链接。
    const auto root = std::filesystem::canonical(project_root, code);
    BuildManifest manifest;
    if (code || !inspect_path(root, last_error_) ||
        !read_manifest(root, manifest_path, selected, manifest, last_error_))
    {
        if (last_error_.empty())
        {
            last_error_ = "cannot resolve project root";
        }
        return false;
    }
    const auto output = root / manifest.output;
    const auto name = PathUtils::to_utf8(output.filename());
    const auto staged = output.parent_path() / PathUtils::from_utf8("." + name + ".staged");
    const auto backup = output.parent_path() / PathUtils::from_utf8("." + name + ".previous");
    ProjectLock package_lock;
    if (!inspect_path(staged, last_error_) || !inspect_path(backup, last_error_) ||
        !package_lock.acquire(
            root / "cache" /
                (".asset-package-" + hash_text(PathUtils::to_utf8(manifest.output)) + ".lock"),
            last_error_) ||
        !recover_output(output, backup, last_error_))
    {
        return false;
    }
    if (!pipeline_.build_assets(root, manifest.platform))
    {
        last_error_ = pipeline_.last_error();
        return false;
    }
    ProjectLock import_lock;
    ResourceCatalog source;
    if (!import_lock.acquire(root / "cache/.asset-build.lock", last_error_) ||
        !ResourceSystem::read_catalog(root / "cache", source) ||
        source.data() != pipeline_.catalog())
    {
        if (last_error_.empty())
        {
            last_error_ = "imported catalog changed before resource packaging";
        }
        return false;
    }
    if (!select_resources(source, manifest, last_error_))
    {
        return false;
    }
    schema::AssetCatalogDataT data;
    data.schema_version = 2;
    data.target_platform = source.data().target_platform;
    ResourceBuildStats stats;
    stats.output_path = output;
    std::map<std::string, std::set<Guid>> files;
    std::string fingerprint = "engine-resource-build-1:" + data.target_platform;
    for (const auto& item : manifest.items)
    {
        std::string key =
            data.target_platform + ":" + item.name + (item.package ? ":package" : ":files");
        for (const auto& guid : item.resources)
        {
            const auto* entry = source.find(guid);
            key += ":" + guid + ":" + entry->content_hash;
        }
        fingerprint += ":" + hash_text(key);
        std::uint64_t offset = 0;
        const auto package_path = "packs/" + item.name + "-" + hash_text("package-1:" + key);
        for (const auto& guid : item.resources)
        {
            auto entry = std::make_unique<schema::AssetEntryT>(*source.find(guid));
            fingerprint += ":" + entry->path + ":" + entry->module_path;
            for (const auto& dependency : entry->dependencies)
            {
                fingerprint += ":" + dependency;
            }
            entry->location->path = item.package ? package_path : "gen/" + guid;
            entry->location->offset = item.package ? offset : 0;
            if (entry->content_size > std::numeric_limits<std::uint64_t>::max() - offset)
            {
                last_error_ = "resource package size overflow";
                return false;
            }
            offset += entry->content_size;
            files[entry->location->path].insert(guid);
            data.entries.push_back(std::move(entry));
        }
        if (item.package && !item.resources.empty())
        {
            ++stats.package_count;
        }
    }
    std::sort(data.entries.begin(), data.entries.end(),
              [](const auto& left, const auto& right) { return left->guid < right->guid; });
    data.revision = hash_text(fingerprint);
    stats.resource_count = data.entries.size();
    stats.revision = data.revision;
    ResourceCatalog result;
    if (!result.assign(data))
    {
        last_error_ = "selected runtime catalog has invalid dependencies or storage locations";
        return false;
    }
    std::filesystem::remove_all(staged, code);
    if (!code)
    {
        std::filesystem::create_directories(staged / "gen", code);
    }
    if (!code)
    {
        std::filesystem::create_directories(staged / "packs", code);
    }
    if (code)
    {
        last_error_ = "cannot prepare resource build staging: " + code.message();
        return false;
    }
    const auto fail = [&]()
    {
        std::error_code cleanup;
        std::filesystem::remove_all(staged, cleanup);
        return false;
    };
    for (const auto& [file, resources] : files)
    {
        const auto destination = staged / PathUtils::from_utf8(file);
        const bool package = file.starts_with("packs/");
        if (verify_output(result, output, resources))
        {
            if (!copy_reusable(output / PathUtils::from_utf8(file), destination, last_error_))
            {
                return fail();
            }
            if (package)
            {
                ++stats.reused_package_count;
            }
            continue;
        }
        std::ofstream stream(destination, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            last_error_ = "cannot create resource build output: " + file;
            return fail();
        }
        for (const auto& guid : resources)
        {
            ByteBuffer entity;
            if (!read_entity(root / "cache", *source.find(guid), entity, last_error_))
            {
                stream.close();
                return fail();
            }
            stream.write(reinterpret_cast<const char*>(entity.data()),
                         static_cast<std::streamsize>(entity.size()));
            if (!stream)
            {
                last_error_ = "cannot write resource build output: " + file;
                stream.close();
                return fail();
            }
        }
        stream.close();
        if (!stream)
        {
            last_error_ = "cannot finish resource build output: " + file;
            return fail();
        }
    }
    for (const auto& [file, resources] : files)
    {
        (void)file;
        if (!verify_output(result, staged, resources))
        {
            last_error_ = "staged resource build failed entity validation";
            return fail();
        }
    }
    ResourceCatalog previous;
    if (ResourceSystem::read_catalog(output, previous) && previous.data() == data)
    {
        bool valid = true;
        for (const auto& [file, resources] : files)
        {
            (void)file;
            valid = valid && verify_output(result, output, resources);
        }
        if (valid)
        {
            std::filesystem::remove_all(staged, code);
            last_build_ = std::move(stats);
            return true;
        }
    }
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishAssetCatalogDataBuffer(builder, schema::AssetCatalogData::Pack(builder, &data));
    if (!write_bytes(staged / "catalog.bin",
                     std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                     last_error_) ||
        !publish_output(staged, output, backup, last_error_))
    {
        return fail();
    }
    last_build_ = std::move(stats);
    return true;
}

const ResourceBuildStats& AssetBuildSystem::last_build() const noexcept
{
    return last_build_;
}

const std::string& AssetBuildSystem::last_error() const noexcept
{
    return last_error_;
}

} // namespace engine
