#include "editor/asset_sync/asset_sync_system.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace engine
{
namespace
{

namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;

void hash_bytes(std::uint64_t& hash, std::string_view bytes)
{
    for (const unsigned char value : bytes)
    {
        hash ^= value;
        hash *= fnv_prime;
    }
}

std::string hex_hash(std::uint64_t hash)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (std::size_t i = result.size(); i != 0; --i)
    {
        result[i - 1] = digits[hash & 15U];
        hash >>= 4U;
    }
    return result;
}

bool file_hash(const fs::path& path, std::string& hash, std::uint64_t& size)
{
    std::vector<std::byte> bytes;
    if (!ResourceSystem::read_binary(path, bytes))
    {
        return false;
    }
    std::uint64_t value = fnv_offset;
    size = bytes.size();
    if (!bytes.empty())
    {
        const auto* characters = reinterpret_cast<const char*>(bytes.data());
        hash_bytes(value, std::string_view(characters, bytes.size()));
    }
    hash = hex_hash(value);
    return true;
}

bool relative_asset_path(std::string_view path)
{
    if (path.empty() || path.front() == '/' || path.find('\\') != path.npos ||
        path.find(':') != path.npos || path.find('\0') != path.npos)
    {
        return false;
    }
    // POSIX 文件名可能不是 UTF-8；清单必须保真，不能以替换字符发布另一条路径。
    const auto encoded = Json(path).dump(-1, ' ', false, Json::error_handler_t::replace);
    const auto checked = Json::parse(encoded, nullptr, false);
    if (!checked.is_string() || checked.get_ref<const std::string&>() != path)
    {
        return false;
    }
    for (const auto& component : PathUtils::from_utf8(path))
    {
        if (component == "." || component == ".." || component.empty())
        {
            return false;
        }
    }
    return true;
}

// 不跟随已有路径中的符号链接，避免写入项目或运行目录以外的位置。
bool no_symlinks(const fs::path& path)
{
    std::error_code error;
    fs::path current;
    for (const auto& component : path)
    {
        current /= component;
        const auto status = fs::symlink_status(current, error);
        if (error == std::errc::no_such_file_or_directory)
        {
            error.clear();
            continue;
        }
        if (error || fs::is_symlink(status))
        {
            return false;
        }
    }
    return true;
}

bool contains_path(const fs::path& parent, const fs::path& child)
{
    return std::mismatch(parent.begin(), parent.end(), child.begin(), child.end()).first ==
           parent.end();
}

bool read_json(const fs::path& path, Json& output)
{
    std::string text;
    if (!ResourceSystem::read_text(path, text))
    {
        return false;
    }
    output = Json::parse(text, nullptr, false);
    return output.is_object();
}

bool write_json(const fs::path& path, const Json& value)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        return false;
    }
    stream << value.dump() << '\n';
    stream.close();
    return !stream.fail();
}

} // namespace

AssetSyncSystem::AssetSyncSystem() noexcept = default;

AssetSyncSystem::~AssetSyncSystem() noexcept
{
    shutdown();
}

bool AssetSyncSystem::on_initialize(const EngineConfig&)
{
    return true;
}

void AssetSyncSystem::on_shutdown() noexcept
{
    revision_.clear();
    last_error_.clear();
}

const std::string& AssetSyncSystem::revision() const noexcept
{
    return revision_;
}

const std::string& AssetSyncSystem::last_error() const noexcept
{
    return last_error_;
}

bool AssetSyncSystem::sync(const fs::path& source_cache, const fs::path& runtime_dir,
                           std::string_view entry)
{
    revision_.clear();
    last_error_.clear();
    std::error_code error;
    fs::path lock;
    fs::path staging;
    fs::path temporary_manifest;
    const auto cleanup = [&]()
    {
        std::error_code ignored;
        if (!temporary_manifest.empty())
        {
            fs::remove(temporary_manifest, ignored);
        }
        if (!staging.empty())
        {
            fs::remove_all(staging, ignored);
        }
        if (!lock.empty())
        {
            fs::remove(lock, ignored);
        }
    };
    const auto fail = [&](std::string message)
    {
        last_error_ = std::move(message);
        cleanup();
        return false;
    };
    if (!is_initialized() || !relative_asset_path(entry))
    {
        return fail("asset sync is not initialized or entry path is invalid");
    }
    const auto source = fs::absolute(source_cache, error).lexically_normal();
    if (error || !no_symlinks(source) || !fs::is_directory(source, error))
    {
        return fail("source assets must be an existing directory without symlinks");
    }
    const auto runtime = fs::absolute(runtime_dir, error).lexically_normal();
    if (error || !no_symlinks(runtime))
    {
        return fail("runtime directory is invalid or contains symlinks");
    }
    const auto assets = runtime / "assets";
    const auto versions = assets / "versions";
    if (contains_path(source, assets) || contains_path(assets, source))
    {
        return fail("source assets and runtime version storage must not overlap");
    }
    if (!no_symlinks(versions))
    {
        return fail("runtime asset directory contains symlinks");
    }
    fs::create_directories(versions, error);
    if (error)
    {
        return fail("cannot create runtime versions: " + error.message());
    }
    const auto lock_path = assets / ".sync-lock";
    if (!fs::create_directory(lock_path, error))
    {
        return fail("another sync is active or an interrupted sync lock remains");
    }
    lock = lock_path;

    std::vector<std::string> paths;
    const bool has_catalog = fs::exists(source / "catalog.bin", error);
    if (error)
    {
        return fail("cannot inspect resource catalog");
    }
    if (has_catalog)
    {
        ResourceCatalog catalog;
        if (!ResourceSystem::read_catalog(source, catalog))
        {
            return fail("source resource catalog or GUID artifacts are invalid");
        }
        paths.emplace_back("catalog.bin");
        for (const auto& asset : catalog.data.entries)
        {
            paths.push_back("gen/" + asset->guid);
        }
    }
    else
    {
        // 兼容旧的独立脚本树；正式编辑器输入为 TaskPipeline 生成的 catalog+gen。
        for (fs::recursive_directory_iterator iterator(source, error), end;
             !error && iterator != end; iterator.increment(error))
        {
            const auto status = iterator->symlink_status(error);
            if (error || fs::is_symlink(status))
            {
                return fail("source assets contain a symlink or unreadable path");
            }
            if (fs::is_regular_file(status))
            {
                const auto relative =
                    PathUtils::to_utf8(iterator->path().lexically_relative(source));
                if (!relative_asset_path(relative))
                {
                    return fail("source contains an invalid relative asset path");
                }
                paths.push_back(relative);
            }
            else if (!fs::is_directory(status))
            {
                return fail("source contains an unsupported asset file type");
            }
        }
        if (error)
        {
            return fail("cannot enumerate source assets: " + error.message());
        }
        if (std::find(paths.begin(), paths.end(), entry) == paths.end())
        {
            return fail("gameplay entry does not exist in source assets");
        }
    }
    std::sort(paths.begin(), paths.end());
    Json files = Json::array();
    for (const auto& relative : paths)
    {
        std::string hash;
        std::uint64_t size = 0;
        if (!file_hash(source / PathUtils::from_utf8(relative), hash, size))
        {
            return fail("cannot read source asset: " + relative);
        }
        files.push_back({{"path", relative}, {"size", size}, {"hash", hash}});
    }

    const auto available_path = assets / "available.json";
    if (!no_symlinks(available_path))
    {
        return fail("available manifest path contains symlinks");
    }
    std::set<std::string> deleted;
    Json previous;
    if (fs::exists(available_path, error))
    {
        if (!read_json(available_path, previous) || !previous.contains("files") ||
            !previous["files"].is_array() || !previous.contains("deleted") ||
            !previous["deleted"].is_array())
        {
            return fail("existing available manifest is invalid");
        }
        for (const auto& item : previous["files"])
        {
            if (!item.is_object() || !item.contains("path") || !item["path"].is_string() ||
                !relative_asset_path(item["path"].get_ref<const std::string&>()))
            {
                return fail("existing manifest contains an invalid file path");
            }
            deleted.insert(item["path"].get<std::string>());
        }
        for (const auto& item : previous["deleted"])
        {
            if (!item.is_string() || !relative_asset_path(item.get_ref<const std::string&>()))
            {
                return fail("existing manifest contains an invalid deleted path");
            }
            deleted.insert(item.get<std::string>());
        }
    }
    else if (error)
    {
        return fail("cannot inspect available manifest");
    }
    for (const auto& relative : paths)
    {
        deleted.erase(relative);
    }
    Json manifest = {{"entry", std::string(entry)}, {"files", files}, {"deleted", deleted}};
    auto hash = fnv_offset;
    hash_bytes(hash, manifest.dump());
    const auto revision = hex_hash(hash);
    manifest["revision"] = revision;
    const auto final_directory = versions / revision;
    if (!no_symlinks(final_directory))
    {
        return fail("immutable version path contains symlinks");
    }
    if (!fs::exists(final_directory, error))
    {
        if (error)
        {
            return fail("cannot inspect immutable version directory");
        }
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto staging_path = assets / (".staging-" + std::to_string(nonce));
        if (!fs::create_directory(staging_path, error))
        {
            return fail("cannot create staging directory");
        }
        staging = staging_path;
        for (const auto& relative : paths)
        {
            const auto target = staging / PathUtils::from_utf8(relative);
            fs::create_directories(target.parent_path(), error);
            if (error || !fs::copy_file(source / PathUtils::from_utf8(relative), target,
                                        fs::copy_options::none, error))
            {
                return fail("cannot copy asset: " + relative);
            }
        }
    }
    const auto& verify_directory = staging.empty() ? final_directory : staging;
    for (const auto& item : files)
    {
        const auto relative = item["path"].get<std::string>();
        std::string actual_hash;
        std::uint64_t actual_size = 0;
        if (!no_symlinks(verify_directory / PathUtils::from_utf8(relative)) ||
            !file_hash(verify_directory / PathUtils::from_utf8(relative), actual_hash,
                       actual_size) ||
            actual_hash != item["hash"].get<std::string>() ||
            actual_size != item["size"].get<std::uint64_t>())
        {
            return fail("asset changed during sync or existing version is corrupt: " + relative);
        }
    }
    if (has_catalog)
    {
        ResourceCatalog verified;
        if (!ResourceSystem::read_catalog(verify_directory, verified))
        {
            return fail("copied resource catalog failed complete artifact validation");
        }
    }
    if (!staging.empty())
    {
        fs::rename(staging, final_directory, error);
        if (error)
        {
            return fail("cannot finalize immutable version: " + error.message());
        }
        staging.clear();
    }
    if (previous != manifest)
    {
        temporary_manifest = assets / ".available-next.json";
        if (!no_symlinks(temporary_manifest) || !write_json(temporary_manifest, manifest))
        {
            return fail("cannot write next available manifest");
        }
        if (!SDL_RenamePath(PathUtils::to_utf8(temporary_manifest).c_str(),
                            PathUtils::to_utf8(available_path).c_str()))
        {
            return fail(std::string("cannot publish available manifest: ") + SDL_GetError());
        }
        temporary_manifest.clear();
    }
    revision_ = revision;
    cleanup();
    return true;
}

} // namespace engine
