#include "editor/asset_pipeline/asset_metadata.hpp"

#include "editor/asset_pipeline/asset_database.hpp"
#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/project_lock.hpp"
#include "runtime/core/guid_map.hpp"
#include "runtime/resource/builtin_resources.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <toml++/toml.hpp>

#include <map>
#include <system_error>

namespace engine::asset_pipeline
{
namespace
{
namespace fs = std::filesystem;

bool read_meta(const fs::path& path, std::string& text, std::string& guid, std::string& error)
{
    if (!ResourceSystem::read_text(path, text))
    {
        error = "Cannot read asset metadata: " + PathUtils::to_utf8(path);
        return false;
    }
    auto parsed = toml::parse(text);
    if (!parsed || parsed["meta_version"].value_or(std::int64_t{}) != 1)
    {
        error = "Invalid asset metadata version: " + PathUtils::to_utf8(path);
        return false;
    }
    guid = parsed["guid"].value_or(std::string{});
    if (!GuidMap::valid_guid(guid))
    {
        guid.clear();
        error = "Invalid asset GUID: " + PathUtils::to_utf8(path);
        return false;
    }
    return true;
}
} // namespace

bool read_or_create_asset_meta(const fs::path& source, const fs::path& staging,
                               std::string_view importer, std::string_view defaults,
                               bool allow_create, std::string& text, std::string& guid,
                               std::string& error)
{
    text.clear();
    guid.clear();
    auto meta = source;
    meta += ".meta";
    std::error_code code;
    const auto status = fs::symlink_status(meta, code);
    const bool missing = code == std::errc::no_such_file_or_directory ||
                         (!code && status.type() == fs::file_type::not_found);
    if (!missing)
    {
        if (code || !fs::is_regular_file(status) || fs::is_symlink(status) ||
            !read_meta(meta, text, guid, error))
        {
            if (error.empty())
            {
                error = "Asset metadata is inaccessible or not a regular file";
            }
            guid.clear();
            return false;
        }
        const auto parsed = toml::parse(text);
        if (parsed["importer"].value_or(std::string{}) != importer)
        {
            guid.clear();
            error =
                "Asset metadata importer does not match its source: " + PathUtils::to_utf8(meta);
            return false;
        }
        return true;
    }
    if (!allow_create)
    {
        error = "Existing asset lost its meta; restore it to preserve references: " +
                PathUtils::to_utf8(source);
        return false;
    }
    if (importer.empty() ||
        importer.find_first_not_of("abcdefghijklmnopqrstuvwxyz_") != std::string_view::npos)
    {
        error = "Invalid asset importer";
        return false;
    }
    code.clear();
    fs::create_directories(staging, code);
    if (code || fs::is_symlink(fs::symlink_status(staging, code)) || code)
    {
        error = "Cannot prepare asset metadata staging";
        return false;
    }
    const auto candidate = new_guid();
    const auto temporary = staging / new_guid();
    const auto content = "guid = \"" + candidate + "\"\nimporter = \"" + std::string(importer) +
                         "\"\nmeta_version = 1\n" + std::string(defaults);
    if (!write_text(temporary, content, error))
    {
        return false;
    }
    fs::rename(temporary, meta, code);
    if (code)
    {
        error = "Cannot publish asset metadata: " + code.message();
        std::error_code cleanup;
        fs::remove(temporary, cleanup);
        return false;
    }
    text = content;
    guid = candidate;
    return true;
}

bool ensure_asset_guid(const fs::path& project_root, const fs::path& source,
                       std::string_view importer, std::string& guid, std::string& error)
{
    guid.clear();
    error.clear();
    std::error_code code;
    const auto root = fs::canonical(project_root, code);
    if (code)
    {
        error = "Cannot resolve asset project root";
        return false;
    }
    const auto assets = root / "assets";
    const auto cache = root / "cache";
    for (const auto& directory : {assets, cache})
    {
        const auto status = fs::symlink_status(directory, code);
        if (code || !fs::is_directory(status) || fs::is_symlink(status))
        {
            error = "Asset identity requires ordinary assets and cache directories";
            return false;
        }
    }
    auto absolute = fs::absolute(source, code).lexically_normal();
    const auto input_root = fs::absolute(project_root, code).lexically_normal();
    const auto input_relative = absolute.lexically_relative(input_root / "assets");
    if (!code && !input_relative.empty() && !input_relative.is_absolute() &&
        *input_relative.begin() != "..")
    {
        absolute = assets / input_relative;
    }
    auto relative = absolute.lexically_relative(assets);
    if (code || relative.empty() || relative.is_absolute() || *relative.begin() == "..")
    {
        error = "Asset source must be inside the project assets directory";
        return false;
    }
    auto cursor = assets;
    for (const auto& part : relative)
    {
        cursor /= part;
        const auto status = fs::symlink_status(cursor, code);
        if (code || fs::is_symlink(status))
        {
            error = "Asset source cannot contain symlinks or inaccessible entries";
            return false;
        }
    }
    if (!fs::is_regular_file(absolute, code) || code)
    {
        error = "Asset source must exist before its metadata is published";
        return false;
    }
    ProjectLock lock;
    if (!lock.acquire(cache / ".asset-build.lock", error))
    {
        return false;
    }
    GuidMap identities;
    for (const auto& entry : builtin_resource_catalog().entries)
    {
        if (!entry || !identities.add(entry->path, entry->guid))
        {
            error = "Invalid builtin resource identities";
            return false;
        }
    }
    fs::recursive_directory_iterator iterator(assets, code);
    const fs::recursive_directory_iterator end;
    while (!code && iterator != end)
    {
        const auto status = iterator->symlink_status(code);
        if (code || fs::is_symlink(status))
        {
            error = "Asset metadata scan rejects inaccessible entries and symlinks";
            return false;
        }
        if (fs::is_regular_file(status) && iterator->path().extension() == ".meta")
        {
            auto asset_path = iterator->path();
            asset_path.replace_extension();
            std::string text;
            std::string identity;
            const auto logical =
                "project/" +
                PathUtils::to_utf8(asset_path.lexically_relative(assets).generic_u8string());
            if (!read_meta(iterator->path(), text, identity, error) ||
                !identities.add(logical, identity))
            {
                if (error.empty())
                {
                    error = "Duplicate resource GUID: " + identity;
                }
                return false;
            }
        }
        iterator.increment(code);
    }
    if (code)
    {
        error = "Cannot scan source metadata: " + code.message();
        return false;
    }
    const auto logical = "project/" + PathUtils::to_utf8(relative.generic_u8string());
    bool allow_create = true;
    if (fs::exists(cache / "catalog.bin", code))
    {
        ResourceCatalog catalog;
        std::string known;
        if (!ResourceSystem::read_catalog(cache, catalog))
        {
            error = "Cannot verify prior resource identity; rebuild or restore the catalog";
            return false;
        }
        allow_create = !catalog.identities().path_to_guid(logical, known);
    }
    if (code)
    {
        error = "Cannot inspect prior catalog";
        return false;
    }
    if (allow_create && fs::exists(cache / "assets.sqlite", code))
    {
        AssetDatabase database;
        std::map<Guid, AssetRecord> previous;
        if (!database.open(cache / "assets.sqlite", error) || !database.load(previous, error))
        {
            return false;
        }
        for (const auto& [identity, record] : previous)
        {
            (void)identity;
            if (record.asset.path == logical)
            {
                allow_create = false;
                break;
            }
        }
    }
    if (code)
    {
        error = "Cannot inspect prior asset database";
        return false;
    }
    std::string text;
    std::string identity;
    if (!read_or_create_asset_meta(absolute, cache / ".asset-meta", importer, {}, allow_create,
                                   text, identity, error))
    {
        return false;
    }
    std::string existing_path;
    if (identities.guid_to_path(identity, existing_path) && existing_path != logical)
    {
        error = "Resource GUID is already assigned to a different source";
        return false;
    }
    guid = std::move(identity);
    return true;
}
} // namespace engine::asset_pipeline
