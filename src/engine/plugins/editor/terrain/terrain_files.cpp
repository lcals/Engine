#include "plugins/editor/terrain/terrain_files.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/project_lock.hpp"
#include "editor/commands/editor_commands.hpp"
#include "plugins/editor/terrain/terrain_source.hpp"
#include "plugins/runtime/terrain/terrain_volume.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_filesystem.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <memory>
#include <system_error>

namespace engine::plugins::terrain
{
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

bool unused(const fs::path& path, std::string& error)
{
    std::error_code status_error;
    const auto status = fs::symlink_status(path, status_error);
    if ((status_error && status_error != std::errc::no_such_file_or_directory) ||
        (!status_error && status.type() != fs::file_type::not_found))
    {
        error = "Terrain source or meta already exists, or its destination cannot be inspected";
        return false;
    }
    return true;
}

bool create_asset(const fs::path& project, const Json& arguments, Json& result, std::string& error)
{
    if (!arguments.is_object() || arguments.size() != 2 || !arguments.contains("path") ||
        !arguments["path"].is_string() || !arguments.contains("source") ||
        !arguments["source"].is_string())
    {
        error = "Terrain creation requires path and source TOML strings";
        return false;
    }
    const auto& relative = arguments["path"].get_ref<const std::string&>();
    const auto& source = arguments["source"].get_ref<const std::string&>();
    schema::TerrainGeneratorDataT generator;
    schema::TerrainAssetDataT volume;
    fs::path destination;
    if (!terrain_file_path(project, "assets", relative, ".terrain", destination, error) ||
        !read_terrain_source(source, generator, error) ||
        !TerrainVolume::generate(generator, volume, error))
    {
        return false;
    }
    editor::asset_pipeline::ProjectLock lock;
    if (!lock.acquire(project / "cache/.asset-build.lock", error))
    {
        return false;
    }
    auto meta = destination;
    meta += ".meta";
    if (!unused(destination, error) || !unused(meta, error))
    {
        return false;
    }
    std::error_code filesystem_error;
    fs::create_directories(destination.parent_path(), filesystem_error);
    if (filesystem_error)
    {
        error = "Cannot create terrain source directory";
        return false;
    }
    const auto guid = editor::asset_pipeline::new_guid();
    if (guid.empty())
    {
        error = "Cannot generate terrain resource GUID";
        return false;
    }
    auto source_temporary = destination;
    source_temporary += ".terrain-write-" + guid + ".meta";
    auto meta_temporary = meta;
    meta_temporary += ".terrain-write-" + guid + ".meta";
    if (!unused(source_temporary, error) || !unused(meta_temporary, error))
    {
        return false;
    }
    const auto metadata = "guid = \"" + guid + "\"\nimporter = \"terrain\"\nmeta_version = 1\n";
    const auto cleanup = [&]()
    {
        std::error_code ignored;
        fs::remove(source_temporary, ignored);
        fs::remove(meta_temporary, ignored);
    };
    if (!editor::asset_pipeline::write_text(source_temporary, source, error) ||
        !editor::asset_pipeline::write_text(meta_temporary, metadata, error))
    {
        cleanup();
        return false;
    }
    if (!SDL_RenamePath(PathUtils::to_utf8(meta_temporary).c_str(),
                        PathUtils::to_utf8(meta).c_str()))
    {
        cleanup();
        error = "Cannot publish terrain meta";
        return false;
    }
    if (!SDL_RenamePath(PathUtils::to_utf8(source_temporary).c_str(),
                        PathUtils::to_utf8(destination).c_str()))
    {
        cleanup();
        fs::remove(meta, filesystem_error);
        error = "Cannot publish terrain source";
        if (filesystem_error)
        {
            error += "; newly created meta could not be removed";
        }
        return false;
    }
    result = {{"path", relative},
              {"guid", guid},
              {"densitySamples", volume.density.size()},
              {"importRequired", true}};
    return true;
}

} // namespace

bool terrain_file_path(const std::filesystem::path& project, std::string_view directory,
                       std::string_view relative, std::string_view extension,
                       std::filesystem::path& output, std::string& error)
{
    output.clear();
    if (relative.empty() || relative.size() > 1024 ||
        std::any_of(relative.begin(), relative.end(), [](unsigned char value)
                    { return value < 0x20 || value == 0x7f || value == ':' || value == '\\'; }))
    {
        error = "Terrain file path requires a bounded relative path without control characters";
        return false;
    }
    const auto path = PathUtils::from_utf8(relative);
    if (path.is_absolute() || path.has_root_name() || path.filename().empty() ||
        PathUtils::to_utf8(path.extension()) != extension ||
        std::any_of(path.begin(), path.end(),
                    [](const auto& part) { return part == ".." || part == "." || part.empty(); }))
    {
        error = "Terrain file path must stay in its project directory and use " +
                std::string(extension);
        return false;
    }
    std::error_code filesystem_error;
    const auto root = fs::canonical(project, filesystem_error);
    if (filesystem_error || !fs::is_directory(root, filesystem_error) || filesystem_error)
    {
        error = "Terrain project directory is unavailable";
        return false;
    }
    const auto candidate = root / PathUtils::from_utf8(directory) / path;
    fs::path cursor = root;
    for (const auto& part : candidate.lexically_relative(root))
    {
        cursor /= part;
        const auto status = fs::symlink_status(cursor, filesystem_error);
        if (filesystem_error && filesystem_error != std::errc::no_such_file_or_directory)
        {
            error = "Cannot inspect terrain file destination";
            return false;
        }
        filesystem_error.clear();
        if (fs::is_symlink(status) || (status.type() != fs::file_type::not_found &&
                                       cursor != candidate && !fs::is_directory(status)))
        {
            error = "Terrain file destination contains a symbolic link or non-directory parent";
            return false;
        }
    }
    output = candidate;
    return true;
}

bool register_terrain_asset_commands(editor::EditorCommands& commands,
                                     const std::filesystem::path& project)
{
    return commands.register_command(
        "create_terrain_asset", false,
        [project](const Json& arguments, Json& result, std::unique_ptr<editor::EditorEdit>&,
                  std::string& error) { return create_asset(project, arguments, result, error); });
}

} // namespace engine::plugins::terrain
