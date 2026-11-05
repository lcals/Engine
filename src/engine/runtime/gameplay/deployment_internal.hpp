#pragma once

#include "runtime/app/app.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <system_error>
#include <utility>

namespace engine::gameplay_detail
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr std::size_t maximum_text_size = 4 * 1024 * 1024;

[[nodiscard]] inline bool read_text(const fs::path& path, std::string& text)
{
    if (ResourceSystem::read_text(path, text) && text.size() <= maximum_text_size)
    {
        return true;
    }
    text.clear();
    return false;
}

[[nodiscard]] inline bool relative_path(std::string_view input, std::string& normalized)
{
    normalized.clear();
    if (input.empty() || input.find_first_of("\\:") != std::string_view::npos ||
        input.find('\0') != std::string_view::npos)
    {
        return false;
    }
    const fs::path path = PathUtils::from_utf8(input);
    if (path.is_absolute() || path.has_root_path())
    {
        return false;
    }
    const auto clean = path.lexically_normal();
    for (const auto& part : clean)
    {
        if (part == "..")
        {
            return false;
        }
    }
    normalized = PathUtils::to_utf8(clean);
    return !normalized.empty() && normalized != "." && normalized.back() != '/';
}

[[nodiscard]] inline bool safe_revision(std::string_view value)
{
    if (value.empty() || value.size() > 128 || value == "." || value == ".." || value == "builtin")
    {
        return false;
    }
    for (const char character : value)
    {
        if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '-' || character == '_' ||
              character == '.'))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool contained_file(const fs::path& root, std::string_view relative,
                                         fs::path& path, bool directory = false)
{
    std::error_code error;
    const auto canonical_root = fs::canonical(root, error);
    if (error || !fs::is_directory(canonical_root, error) || error)
    {
        return false;
    }
    const auto candidate = fs::canonical(canonical_root / PathUtils::from_utf8(relative), error);
    if (error)
    {
        return false;
    }
    auto component = candidate.begin();
    for (const auto& root_component : canonical_root)
    {
        if (component == candidate.end() || *component != root_component)
        {
            return false;
        }
        ++component;
    }
    const bool correct_type =
        directory ? fs::is_directory(candidate, error) : fs::is_regular_file(candidate, error);
    if (component == candidate.end() || !correct_type || error)
    {
        return false;
    }
    path = candidate;
    return true;
}

[[nodiscard]] inline bool matches_file(const fs::path& path, std::uint64_t size,
                                       std::string_view hash)
{
    std::uint64_t value = 0;
    std::uint64_t actual_size = 0;
    if (hash.size() != 16 || !VirtualFileSystem::hash_file(path, value, actual_size) ||
        actual_size != size)
    {
        return false;
    }
    constexpr char hex[] = "0123456789abcdef";
    std::array<char, 16> actual_hash{};
    for (std::size_t index = actual_hash.size(); index > 0; --index)
    {
        actual_hash[index - 1] = hex[value & 15];
        value >>= 4;
    }
    return hash == std::string_view(actual_hash.data(), actual_hash.size());
}

[[nodiscard]] inline bool script_path(std::string_view path)
{
    return path.ends_with(".js") || path.ends_with(".mjs");
}

struct Snapshot
{
    ResourceCatalog catalog;
    MountList mounts;
    std::string revision;
    std::string entry = "scripts/game.js";
};

struct ControlRead
{
    std::string text;
    fs::file_time_type written{};
    bool exists = false;
    bool valid = false;
    bool hot_reload = false;
    std::string error;
};

inline ControlRead read_control(const fs::path& root)
{
    ControlRead output;
    const auto file = root / "script-control.json";
    std::error_code error;
    output.exists = fs::exists(file, error);
    if (!output.exists && !error)
    {
        return output;
    }
    if (error || !read_text(file, output.text))
    {
        output.error = "Cannot read script control";
        return output;
    }
    output.written = fs::last_write_time(file, error);
    if (error)
    {
        output.error = "Cannot inspect script control timestamp";
        return output;
    }
    const auto document = Json::parse(output.text, nullptr, false);
    if (!document.is_object() || !document.contains("hotReload") ||
        !document["hotReload"].is_boolean())
    {
        output.error = "Script control requires a boolean hotReload";
        return output;
    }
    output.valid = true;
    output.hot_reload = document["hotReload"].get<bool>();
    return output;
}

inline bool write_status(const fs::path& root, const std::string& text, std::string& message)
{
    const auto target = root / "script-status.json";
    const auto temporary = root / "script-status.json.tmp";
    std::error_code error;
    fs::remove(temporary, error);
    if (error)
    {
        message = "Cannot remove temporary script status: " + error.message();
        return false;
    }
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file << text;
    file.close();
    if (!file)
    {
        message = "Cannot write script status";
        return false;
    }
    if (!SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(), PathUtils::to_utf8(target).c_str()))
    {
        message = std::string("Cannot publish script status: ") + SDL_GetError();
        return false;
    }
    return true;
}

struct DeploymentRead
{
    std::string last_manifest;
    std::string current_revision;
    std::string target_platform;
    std::unique_ptr<Snapshot> available;
    std::string error;

    bool fail(std::string message)
    {
        error = std::move(message);
        return false;
    }

    [[nodiscard]] bool read(const fs::path& persistent_root,
                            const std::atomic<bool>* cancelled = nullptr)
    {
        const fs::path root = persistent_root / "assets";
        const fs::path manifest = root / "available.json";
        std::error_code error;
        if (!fs::exists(manifest, error) && !error)
        {
            // 已接受的完整版本不因发布文件暂时缺失而退回内置版本。
            return true;
        }
        std::string text;
        if (error || !read_text(manifest, text))
        {
            return fail("Cannot read asset version manifest");
        }
        if (text == last_manifest)
        {
            return true;
        }
        const auto document = Json::parse(text, nullptr, false);
        if (!document.is_object() || !document.contains("revision") ||
            !document["revision"].is_string() || !document.contains("entry") ||
            !document["entry"].is_string() || !document.contains("files") ||
            !document["files"].is_array() || !document.contains("deleted") ||
            !document["deleted"].is_array())
        {
            return fail("Invalid asset version manifest");
        }
        auto candidate = std::make_unique<Snapshot>();
        candidate->revision = document["revision"].get<std::string>();
        const std::string entry = document["entry"].get<std::string>();
        if (!safe_revision(candidate->revision) || !relative_path(entry, candidate->entry) ||
            entry != candidate->entry || !script_path(entry))
        {
            return fail("Unsafe asset revision or entry path");
        }
        if (candidate->revision == current_revision)
        {
            return fail("A published revision is immutable; publish a new revision");
        }
        fs::path version_root;
        if (!contained_file(persistent_root, "assets/versions/" + candidate->revision, version_root,
                            true))
        {
            return fail("Missing or escaping asset version directory");
        }
        std::set<std::string> seen;
        for (const auto& item : document["files"])
        {
            if (cancelled && cancelled->load(std::memory_order_relaxed))
            {
                return fail("Deployment verification cancelled");
            }
            if (!item.is_object() || !item.contains("path") || !item["path"].is_string() ||
                !item.contains("size") || !item["size"].is_number_unsigned() ||
                !item.contains("hash") || !item["hash"].is_string())
            {
                return fail("Invalid version file description");
            }
            const auto path = item["path"].get<std::string>();
            std::string normalized;
            fs::path file;
            if (!relative_path(path, normalized) || normalized != path ||
                !seen.insert(path).second || !contained_file(version_root, path, file))
            {
                return fail("Asset version file failed path, size or hash check: " + path);
            }
            if (!matches_file(file, item["size"].get<std::uint64_t>(),
                              item["hash"].get<std::string>()))
            {
                return fail("Version asset failed size or hash check: " + path);
            }
        }
        std::set<std::string> deleted;
        for (const auto& item : document["deleted"])
        {
            if (!item.is_string())
            {
                return fail("Invalid deleted asset path");
            }
            const auto path = item.get<std::string>();
            std::string normalized;
            if (!relative_path(path, normalized) || normalized != path || seen.contains(path) ||
                !deleted.insert(path).second)
            {
                return fail("Unsafe or conflicting deleted asset path: " + path);
            }
        }
        if (!seen.contains("catalog.bin") ||
            !ResourceSystem::read_catalog(version_root, candidate->catalog))
        {
            return fail("Published resources require a valid runtime catalog");
        }
        if (candidate->catalog.data().target_platform != target_platform)
        {
            return fail("Published resource catalog targets a different platform");
        }
        candidate->mounts = {{"content", version_root}};
        bool has_entry = false;
        std::set<std::string> expected{"catalog.bin"};
        for (const auto& asset : candidate->catalog.data().entries)
        {
            if (!asset->location || asset->location->mount != "content")
            {
                return fail("Deployment catalog contains an unsupported mount");
            }
            expected.insert(asset->location->path);
            has_entry = has_entry || (asset->kind == schema::AssetKind::script &&
                                      asset->module_path == candidate->entry);
        }
        if (!has_entry || seen != expected)
        {
            return fail("Version manifest does not match the complete runtime catalog");
        }
        last_manifest = std::move(text);
        available = std::move(candidate);
        return true;
    }
};

} // namespace engine::gameplay_detail
