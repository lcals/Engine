#include "editor/asset_pipeline/mesh_lod_commands.hpp"
#include "editor/file_text.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "editor/asset_pipeline/project_lock.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/mesh/mesh_lod_settings.hpp"
#include "editor/mesh/mesh_lods.hpp"
#include "runtime/core/guid_map.hpp"
#include "runtime/resource/codecs/mesh_resource_codec.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_filesystem.h>
#include <flatbuffers/verifier.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <system_error>

namespace engine::editor
{
namespace
{
using Json = nlohmann::json;
namespace fs = std::filesystem;

bool source_meta(const fs::path& project, const Json& arguments, bool writing, fs::path& source,
                 fs::path& meta_path, toml::table& meta, MeshLodSettings& settings,
                 std::string& error)
{
    if (!arguments.is_object() || !arguments.contains("path") || !arguments["path"].is_string())
    {
        error = "Mesh LOD import commands require a source asset path";
        return false;
    }
    for (const auto& [key, unused] : arguments.items())
    {
        (void)unused;
        if (key != "path" && (!writing || (key != "enabled" && key != "triangleRatios" &&
                                           key != "screenHeights" && key != "maxRelativeError")))
        {
            error = "Unknown mesh LOD import argument: " + key;
            return false;
        }
    }
    const auto path = arguments["path"].get<std::string>();
    auto extension = PathUtils::to_utf8(PathUtils::from_utf8(path).extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".mesh" && extension != ".fbx" && extension != ".usd" &&
        extension != ".usda" && extension != ".usdc")
    {
        error = "LOD import source must be a mesh, FBX or USD asset";
        return false;
    }
    const auto exact_extension = PathUtils::to_utf8(PathUtils::from_utf8(path).extension());
    if (!asset_pipeline::project_file_path(project, "assets", path, exact_extension, source,
                                           error) ||
        !asset_pipeline::project_file_path(project, "assets", path + ".meta", ".meta", meta_path,
                                           error))
    {
        return false;
    }
    std::error_code code;
    if (!fs::is_regular_file(source, code) || code)
    {
        error = "Mesh LOD import source does not exist";
        return false;
    }
    std::string text;
    if (!engine::editor::file_detail::read_text(meta_path, text))
    {
        error = "Mesh LOD import requires existing adjacent meta and stable GUID";
        return false;
    }
    auto parsed = toml::parse(text);
    if (!parsed || parsed["meta_version"].value_or(std::int64_t{}) != 1 ||
        !GuidMap::valid_guid(parsed["guid"].value_or(std::string{})))
    {
        error = "Mesh LOD import source meta is invalid";
        return false;
    }
    const auto importer = parsed["importer"].value_or(std::string{});
    if ((extension == ".mesh" && importer != "mesh") ||
        (extension == ".fbx" && importer != "fbx") ||
        ((extension == ".usd" || extension == ".usda" || extension == ".usdc") &&
         importer != "usd"))
    {
        error = "Mesh LOD import source meta has the wrong importer";
        return false;
    }
    meta = std::move(parsed.table());
    return read_mesh_lod_settings(meta, settings, error);
}

Json settings_json(const MeshLodSettings& settings)
{
    return {{"enabled", settings.enabled},
            {"triangleRatios", settings.triangle_ratios},
            {"screenHeights", settings.screen_heights},
            {"maxRelativeError", settings.max_relative_error}};
}

bool mesh_results(const fs::path& project, std::string_view path, const MeshLodSettings& settings,
                  Json& meshes, std::string& error)
{
    meshes = Json::array();
    ResourceCatalog catalog;
    if (!ResourceSystem::read_catalog(project / "cache", catalog))
    {
        std::error_code code;
        const bool exists = fs::exists(project / "cache/catalog.bin", code);
        if (code || exists)
        {
            error = "Cannot read the published catalog for mesh LOD results";
            return false;
        }
        return true;
    }
    const std::string prefix = std::string(path) + "/";
    for (const auto& entry : catalog.data().entries)
    {
        if (entry->kind != schema::AssetKind::mesh ||
            (entry->path != path && !entry->path.starts_with(prefix)))
        {
            continue;
        }
        ByteBuffer payload;
        if (!ResourceSystem::read_resource(catalog, {{"content", project / "cache"}}, entry->guid,
                                           payload))
        {
            error = "Cannot read imported mesh LOD result: " + entry->path;
            return false;
        }
        flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                       payload.size());
        if (!schema::VerifyMeshDataBuffer(verifier))
        {
            error = "Imported mesh LOD result has invalid FlatBuffers data";
            return false;
        }
        auto mesh = schema::UnPackMeshData(payload.data());
        if (!MeshResourceCodec::validate(*mesh))
        {
            error = "Imported mesh LOD result failed mesh validation";
            return false;
        }
        Json levels = Json::array({{{"level", 0}, {"triangleCount", mesh->indices.size() / 3}}});
        for (std::size_t level = 0; level < mesh->lods.size(); ++level)
        {
            levels.push_back({{"level", level + 1},
                              {"triangleCount", mesh->lods[level]->indices.size() / 3},
                              {"screenHeight", mesh->lods[level]->screen_height}});
        }
        const std::string reason = !mesh->skin.empty()  ? "skinned_mesh"
                                   : !settings.enabled  ? "generation_disabled"
                                   : mesh->lods.empty() ? "topology_or_error_limit"
                                                        : "";
        meshes.push_back({{"path", entry->path},
                          {"guid", entry->guid},
                          {"levels", std::move(levels)},
                          {"skippedReason", reason}});
    }
    return true;
}

bool read(const fs::path& project, const Json& arguments, Json& result, std::string& error)
{
    fs::path source, meta_path;
    toml::table meta;
    MeshLodSettings settings;
    if (!source_meta(project, arguments, false, source, meta_path, meta, settings, error))
    {
        return false;
    }
    Json meshes;
    if (!mesh_results(project, arguments["path"].get<std::string>(), settings, meshes, error))
    {
        return false;
    }
    result = {{"path", arguments["path"]},
              {"guid", meta["guid"].value_or(std::string{})},
              {"settings", settings_json(settings)},
              {"meshes", std::move(meshes)}};
    return true;
}

bool write(AssetPipelineSystem& pipeline, const fs::path& project, const Json& arguments,
           Json& result, std::string& error)
{
    bool changed = false;
    {
        asset_pipeline::ProjectLock lock;
        if (!lock.acquire(project / "cache" / ".asset-build.lock", error))
        {
            return false;
        }
        fs::path source, meta_path;
        toml::table meta;
        MeshLodSettings previous;
        if (!source_meta(project, arguments, true, source, meta_path, meta, previous, error))
        {
            return false;
        }
        toml::table options;
        if (const auto* existing = meta["lod"].as_table())
        {
            options = *existing;
        }
        if (arguments.contains("enabled"))
        {
            if (!arguments["enabled"].is_boolean())
            {
                error = "enabled must be boolean";
                return false;
            }
            options.insert_or_assign("enabled", arguments["enabled"].get<bool>());
        }
        for (const auto& [json_key, meta_key] : {std::pair{"triangleRatios", "triangle_ratios"},
                                                 std::pair{"screenHeights", "screen_heights"}})
        {
            if (!arguments.contains(json_key))
            {
                continue;
            }
            if (!arguments[json_key].is_array())
            {
                error = std::string(json_key) + " must be a number array";
                return false;
            }
            toml::array values;
            for (const auto& value : arguments[json_key])
            {
                if (!value.is_number() || !std::isfinite(value.get<double>()))
                {
                    error = std::string(json_key) + " must contain finite numbers";
                    return false;
                }
                values.push_back(value.get<double>());
            }
            options.insert_or_assign(meta_key, std::move(values));
        }
        if (arguments.contains("maxRelativeError"))
        {
            if (!arguments["maxRelativeError"].is_number() ||
                !std::isfinite(arguments["maxRelativeError"].get<double>()))
            {
                error = "maxRelativeError must be a finite number";
                return false;
            }
            options.insert_or_assign("max_relative_error",
                                     arguments["maxRelativeError"].get<double>());
        }
        meta.insert_or_assign("lod", std::move(options));
        MeshLodSettings next;
        if (!read_mesh_lod_settings(meta, next, error))
        {
            return false;
        }
        changed = settings_json(previous) != settings_json(next);
        if (changed)
        {
            std::ostringstream formatted;
            formatted << toml::toml_formatter(meta);
            const auto identity = asset_pipeline::new_guid();
            if (identity.empty())
            {
                error = "Cannot allocate mesh LOD metadata staging identity";
                return false;
            }
            auto temporary = meta_path;
            temporary += ".lod-write-" + identity;
            if (!asset_pipeline::write_text(temporary, formatted.str(), error) ||
                !SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(),
                                PathUtils::to_utf8(meta_path).c_str()))
            {
                std::error_code cleanup;
                fs::remove(temporary, cleanup);
                if (error.empty())
                {
                    error = "Cannot atomically publish mesh LOD import meta";
                }
                return false;
            }
        }
    }
    // The existing pipeline owns import jobs and publication, after the meta edit lock is released.
    if (changed && !pipeline.build_assets(project))
    {
        result = {{"saved", true}, {"imported", false}};
        error = pipeline.last_error();
        return false;
    }
    if (!read(project, {{"path", arguments["path"]}}, result, error))
    {
        return false;
    }
    result["changed"] = changed;
    result["saved"] = true;
    result["imported"] = changed;
    if (changed)
    {
        result["revision"] = pipeline.last_build().revision;
    }
    return true;
}

} // namespace

bool register_mesh_lod_commands(EditorCommands& commands, AssetPipelineSystem& pipeline,
                                fs::path project_root)
{
    return commands.register_command("get_mesh_lod_import", false,
                                     [project_root](const Json& arguments, Json& result,
                                                    std::unique_ptr<EditorEdit>&,
                                                    std::string& error)
                                     { return read(project_root, arguments, result, error); }) &&
           commands.register_command(
               "set_mesh_lod_import", false,
               [project_root, &pipeline](const Json& arguments, Json& result,
                                         std::unique_ptr<EditorEdit>&, std::string& error)
               { return write(pipeline, project_root, arguments, result, error); });
}

} // namespace engine::editor
