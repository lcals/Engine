#include "editor/scene/scene_persistence.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/asset_metadata.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/editor_edit.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/schema/shader_generated.h"
#include "runtime/utils/path_utils.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL_filesystem.h>
#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#include <charconv>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace engine::editor
{
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

bool no_symlinks(const fs::path& path)
{
    fs::path cursor;
    for (const auto& part : path)
    {
        cursor /= part;
        std::error_code ec;
        const auto status = fs::symlink_status(cursor, ec);
        if ((ec && ec != std::errc::no_such_file_or_directory) || fs::is_symlink(status))
        {
            return false;
        }
    }
    return true;
}

bool scene_path(const fs::path& root, const std::string& text, fs::path& output, std::string& error)
{
    const auto relative = PathUtils::from_utf8(text);
    if (text.empty() || text.size() > 1024 || text.find('\0') != std::string::npos ||
        text.find_first_of("\\:") != std::string::npos || relative.is_absolute() ||
        relative.has_root_name() || relative.extension() != ".scene")
    {
        error = "Scene path must be a relative assets path ending in .scene";
        return false;
    }
    for (const auto& part : relative)
    {
        if (part == ".." || part == ".")
        {
            error = "Scene path must not contain traversal components";
            return false;
        }
    }
    output = root / "assets" / relative;
    if (!no_symlinks(output))
    {
        error = "Scene path must not contain symbolic links";
        return false;
    }
    return true;
}

struct SavedFile
{
    fs::path destination;
    fs::path backup;
    bool existed = false;
    fs::file_time_type modified{};
};

class SourceTransaction final
{
public:
    fs::path staging;
    std::vector<SavedFile> files;
    bool preserve_staging = false;

    ~SourceTransaction() noexcept
    {
        if (!preserve_staging)
        {
            std::error_code ec;
            fs::remove_all(staging, ec);
        }
    }

    bool remember(const fs::path& path, std::string& error)
    {
        if (!no_symlinks(path))
        {
            error = "Scene resource path contains symbolic links";
            return false;
        }
        for (const auto& file : files)
        {
            if (file.destination == path)
            {
                return true;
            }
        }
        std::error_code ec;
        SavedFile file;
        file.destination = path;
        file.backup = staging / ("backup-" + std::to_string(files.size()));
        file.existed = fs::exists(path, ec);
        if (ec ||
            (file.existed && (!fs::is_regular_file(path, ec) || ec ||
                              !fs::copy_file(path, file.backup, fs::copy_options::none, ec) || ec)))
        {
            error = "Cannot preserve previous scene source";
            return false;
        }
        if (file.existed)
        {
            file.modified = fs::last_write_time(path, ec);
            if (ec)
            {
                error = "Cannot inspect previous scene source timestamp";
                return false;
            }
            fs::last_write_time(file.backup, file.modified, ec);
            if (ec)
            {
                error = "Cannot preserve previous scene source timestamp";
                return false;
            }
        }
        files.push_back(std::move(file));
        return true;
    }

    bool publish(const fs::path& temporary, const fs::path& path, bool& changed, std::string& error)
    {
        changed = false;
        if (!no_symlinks(path))
        {
            error = "Scene resource path contains symbolic links";
            return false;
        }
        std::error_code ec;
        const bool exists = fs::exists(path, ec);
        if (ec)
        {
            error = "Cannot inspect scene resource destination";
            return false;
        }
        if (exists)
        {
            ByteBuffer candidate;
            ByteBuffer previous;
            if (!fs::is_regular_file(path, ec) || ec ||
                !ResourceSystem::read_binary(temporary, candidate) ||
                !ResourceSystem::read_binary(path, previous))
            {
                error = "Cannot compare scene resource content";
                return false;
            }
            if (candidate == previous)
            {
                return true;
            }
        }
        if (!remember(path, error))
        {
            return false;
        }
        fs::create_directories(path.parent_path(), ec);
        if (ec || !SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(),
                                 PathUtils::to_utf8(path).c_str()))
        {
            error = "Cannot publish scene resource";
            return false;
        }
        changed = true;
        return true;
    }

    bool rollback() noexcept
    {
        bool result = true;
        for (auto item = files.rbegin(); item != files.rend(); ++item)
        {
            std::error_code ec;
            if (item->existed)
            {
                if (!SDL_RenamePath(PathUtils::to_utf8(item->backup).c_str(),
                                    PathUtils::to_utf8(item->destination).c_str()))
                {
                    result = false;
                }
            }
            else
            {
                fs::remove(item->destination, ec);
                result = result && !ec;
            }
        }
        return result;
    }
};

bool write_material(const Material& material, const fs::path& path, std::string& error)
{
    const auto* shader = material.shader_data();
    Guid shader_guid;
    if (!shader || !ResourceSystem::path_to_guid(material.shader_path(), shader_guid))
    {
        error = "Cannot resolve material shader identity";
        return false;
    }
    toml::table document;
    document.insert("shader", shader_guid);
    document.insert("cull", schema::EnumNameCullMode(material.cull_mode()));
    toml::array parameters;
    for (const auto& parameter : shader->parameters)
    {
        if (!parameter)
        {
            error = "Invalid shader parameter";
            return false;
        }
        if (!parameter->exposed)
        {
            continue;
        }
        toml::table value;
        value.insert("name", parameter->name);
        bool valid = false;
        switch (parameter->default_value.type)
        {
        case schema::MaterialValue::FloatValue:
        {
            float number = 0;
            valid = material.get_float(parameter->name, number);
            value.insert("type", "float");
            value.insert("value", static_cast<double>(number));
            break;
        }
        case schema::MaterialValue::Vec2Value:
        {
            Vec2 number;
            valid = material.get_v2(parameter->name, number);
            value.insert("type", "v2");
            value.insert("value", toml::array{number.x(), number.y()});
            break;
        }
        case schema::MaterialValue::Vec3Value:
        {
            Vec3 number;
            valid = material.get_v3(parameter->name, number);
            value.insert("type", "v3");
            value.insert("value", toml::array{number.x(), number.y(), number.z()});
            break;
        }
        case schema::MaterialValue::Vec4Value:
        {
            Vec4 number;
            valid = material.get_v4(parameter->name, number);
            value.insert("type", "v4");
            value.insert("value", toml::array{number.x(), number.y(), number.z(), number.w()});
            break;
        }
        default:
            break;
        }
        if (!valid)
        {
            error = "Cannot read material parameter: " + parameter->name;
            return false;
        }
        parameters.push_back(std::move(value));
    }
    document.insert("parameters", std::move(parameters));
    toml::array textures;
    for (const auto& sampler : shader->samplers)
    {
        if (sampler && !sampler->exposed)
        {
            continue;
        }
        std::string_view texture;
        Guid guid;
        if (!sampler || !material.get_texture(sampler->name, texture) ||
            !ResourceSystem::path_to_guid(texture, guid))
        {
            error = "Cannot resolve material texture identity";
            return false;
        }
        toml::table entry;
        entry.insert("name", sampler->name);
        entry.insert("texture", guid);
        textures.push_back(std::move(entry));
    }
    document.insert("textures", std::move(textures));
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << document;
    output.close();
    if (!output)
    {
        error = "Cannot write material source";
        return false;
    }
    return true;
}

bool source_digest(const fs::path& path, std::string& digest, std::string& error)
{
    ByteBuffer content;
    if (!no_symlinks(path) || !ResourceSystem::read_binary(path, content))
    {
        error = "Cannot read saved scene resource: " + PathUtils::to_utf8(path);
        return false;
    }
    digest = asset_pipeline::hash_bytes(content);
    return true;
}

bool existing_guid(const fs::path& source, std::string_view importer, Guid& guid,
                   std::string& meta_hash, std::string& error)
{
    std::string text;
    if (!asset_pipeline::read_or_create_asset_meta(source, {}, importer, {}, false, text, guid,
                                                  error))
    {
        return false;
    }
    meta_hash = asset_pipeline::hash_text(text);
    return true;
}
} // namespace

struct ScenePersistence::State
{
    struct MeshSnapshot
    {
        std::uint64_t scene_id = 0;
        std::uint64_t instance_id = 0;
        std::uint64_t revision = 0;
    };

    std::map<fs::path, MeshSnapshot> meshes;
    std::map<fs::path, SceneSavedResource> sources;
    std::uint64_t saved_scene_id = 0;

    bool matches_saved_source(const fs::path& path, std::uint64_t scene_id,
                              std::string& error) const;
    bool save(WorldSystem& world, const fs::path& project_root, const MeshSourceWriter& writer,
              const Json& arguments, Json& result, std::vector<SceneSavedResource>& changes,
              std::string& error);
};

bool ScenePersistence::State::matches_saved_source(const fs::path& path,
                                                    std::uint64_t scene_id,
                                                    std::string& error) const
{
    if (saved_scene_id != scene_id)
    {
        return true;
    }
    const auto known = sources.find(path);
    if (known == sources.end())
    {
        return true;
    }
    auto meta = path;
    meta += ".meta";
    std::string source_hash;
    std::string meta_hash;
    if (!source_digest(path, source_hash, error) || !source_digest(meta, meta_hash, error) ||
        source_hash != known->second.source_hash || meta_hash != known->second.meta_hash)
    {
        error = "Scene save conflicts with external source or meta changes: " +
                PathUtils::to_utf8(path) + "; refresh the scene or restore the previous source";
        return false;
    }
    return true;
}

bool ScenePersistence::State::save(WorldSystem& world, const fs::path& project_root,
                                    const MeshSourceWriter& writer, const Json& arguments,
                                    Json& result, std::vector<SceneSavedResource>& changes,
                                    std::string& error)
{
    changes.clear();
    if (!arguments.is_object() || arguments.size() != 2 || !arguments.contains("sceneId") ||
        !arguments["sceneId"].is_string() || !arguments.contains("path") ||
        !arguments["path"].is_string())
    {
        error = "save_scene requires sceneId and path";
        return false;
    }
    const auto& identity = arguments["sceneId"].get_ref<const std::string&>();
    std::uint64_t scene_id = 0;
    const auto parsed =
        std::from_chars(identity.data(), identity.data() + identity.size(), scene_id);
    const auto* scene = world.scene();
    if (parsed.ec != std::errc{} || parsed.ptr != identity.data() + identity.size() ||
        identity != std::to_string(scene_id) || !scene || scene->id() != scene_id)
    {
        error = "save_scene requires the current sceneId";
        return false;
    }
    std::error_code ec;
    const auto root = fs::canonical(project_root, ec);
    fs::path destination;
    const auto relative = arguments["path"].get<std::string>();
    if (ec || !scene_path(root, relative, destination, error) || !no_symlinks(root / "cache"))
    {
        if (error.empty())
        {
            error = "Cannot resolve project source and cache directories";
        }
        return false;
    }
    if (!matches_saved_source(destination, scene_id, error))
    {
        return false;
    }
    fs::create_directories(destination.parent_path(), ec);
    if (ec)
    {
        error = "Cannot prepare scene source directory";
        return false;
    }
    SourceTransaction transaction;
    // 同一场景目录内暂存，避免 assets 与 cache 位于不同卷时无法原子替换。
    transaction.staging = destination.parent_path() / (".scene-save-" + asset_pipeline::new_guid());
    if (!fs::create_directory(transaction.staging, ec) || ec)
    {
        error = "Cannot create scene staging directory";
        return false;
    }
    auto resource_name = destination.stem();
    resource_name += "-resources";
    const auto resource_root = destination.parent_path() / resource_name;
    std::size_t resource_count = 0;
    std::size_t mesh_count = 0;
    std::size_t material_count = 0;
    std::size_t reused_mesh_count = 0;
    std::size_t reused_material_count = 0;
    std::map<fs::path, MeshSnapshot> saved_meshes;
    std::map<fs::path, SceneSavedResource> saved_sources;
    std::set<std::string, std::less<>> referenced_sources;
    for (const auto& node : scene->nodes())
    {
        const auto* renderer = node->get_component<MeshRenderer>();
        if (!renderer)
        {
            continue;
        }
        if (!renderer->mesh_path().empty())
        {
            referenced_sources.emplace(renderer->mesh_path());
        }
        for (std::size_t slot = 0; slot < renderer->material_count(); ++slot)
        {
            referenced_sources.emplace(renderer->material(slot)->type_path());
        }
    }
    // 已加载的资源可能被多个节点共享；覆盖值只能写入独立的本次导出目标。
    const auto export_path = [&](const fs::path& folder, const std::string& name,
                                 std::string_view extension, fs::path& output)
    {
        for (std::size_t suffix = 0; suffix < 4096; ++suffix)
        {
            const auto filename = name + (suffix ? "-override-" + std::to_string(suffix) : "") +
                                  std::string(extension);
            const auto candidate = folder / filename;
            const auto logical = "project/" + PathUtils::to_utf8(
                candidate.lexically_relative(root / "assets").generic_u8string());
            if (referenced_sources.contains(logical))
            {
                continue;
            }
            if (!no_symlinks(candidate))
            {
                error = "Scene resource destination contains symbolic links";
                return false;
            }
            std::error_code code;
            const bool exists = fs::exists(candidate, code);
            if (code)
            {
                error = "Cannot inspect scene resource destination";
                return false;
            }
            if (!exists || (saved_scene_id == scene_id && sources.contains(candidate)))
            {
                output = candidate;
                return true;
            }
        }
        error = "Cannot allocate an independent scene resource destination";
        return false;
    };
    const auto resolve_guid = [&](const fs::path& path, std::string_view importer, Guid& guid,
                                  std::string& meta_hash, bool& changed)
    {
        auto meta = path;
        meta += ".meta";
        if (!no_symlinks(path) || !no_symlinks(meta))
        {
            error = "Scene resource path contains symbolic links";
            return false;
        }
        std::error_code code;
        const bool exists = fs::exists(meta, code);
        if (code)
        {
            error = "Cannot inspect scene resource metadata";
            return false;
        }
        const auto known = saved_scene_id == scene_id ? sources.find(path) : sources.end();
        if (exists)
        {
            if (!existing_guid(path, importer, guid, meta_hash, error))
            {
                return false;
            }
            const auto logical = "project/" + PathUtils::to_utf8(
                path.lexically_relative(root / "assets").generic_u8string());
            std::string assigned_path;
            if ((known != sources.end() && guid != known->second.guid) ||
                (ResourceSystem::guid_to_path(guid, assigned_path) && assigned_path != logical))
            {
                error = "Saved scene resource identity changed or conflicts with another asset";
                return false;
            }
        }
        else
        {
            if (known != sources.end())
            {
                error = "Saved scene resource lost its meta; restore it to preserve references";
                return false;
            }
            if (!transaction.remember(meta, error) ||
                !asset_pipeline::ensure_asset_guid(root, path, importer, guid, error))
            {
                return false;
            }
            // ensure_asset_guid 新建 meta 的固定格式；不把发布后的外部编辑当成本次写入。
            const auto metadata = "guid = \"" + guid + "\"\nimporter = \"" +
                                  std::string(importer) + "\"\nmeta_version = 1\n";
            meta_hash = asset_pipeline::hash_text(metadata);
            changed = true;
        }
        return true;
    };
    const auto publish = [&](const fs::path& staged, const fs::path& path,
                             std::string_view importer, Guid& guid, bool& changed)
    {
        SceneSavedResource published;
        published.source = path;
        if (!matches_saved_source(path, scene_id, error) ||
            !source_digest(staged, published.source_hash, error) ||
            !transaction.publish(staged, path, changed, error) ||
            !resolve_guid(path, importer, guid, published.meta_hash, changed))
        {
            return false;
        }
        published.guid = guid;
        saved_sources[path] = published;
        if (changed)
        {
            changes.push_back(std::move(published));
            ++resource_count;
        }
        return true;
    };
    const auto rollback = [&]()
    {
        if (!transaction.rollback())
        {
            transaction.preserve_staging = true;
            error += "; failed to restore previous files; backups retained at " +
                     PathUtils::to_utf8(transaction.staging);
        }
        return false;
    };
    schema::SceneDataT data;
    data.info = std::make_unique<schema::SceneInfoT>();
    data.info->name = scene->name();
    data.info->active_camera_node =
        scene->active_camera() ? scene->active_camera()->node().info().id : 0;
    for (const auto& node : scene->nodes())
    {
        auto saved = std::make_unique<schema::NodeDataT>();
        saved->info = std::make_unique<schema::NodeInfoT>(node->info());
        for (const auto& component : node->components())
        {
            auto value = std::make_unique<schema::ComponentDataT>(component->data());
            if (component->type() != ComponentType::MeshRendererData &&
                !component->build_data(*value))
            {
                error = "Cannot save component data";
                return rollback();
            }
            if (component->type() == ComponentType::MeshRendererData)
            {
                const auto* renderer = node->get_component<MeshRenderer>();
                auto* mesh = value->payload.AsMeshRendererData();
                if (!renderer || !mesh)
                {
                    error = "Cannot save mesh renderer data";
                    return rollback();
                }
                const auto name = "node-" + std::to_string(node->info().id);
                if (!renderer->mesh_path().empty())
                {
                    if (!ResourceSystem::path_to_guid(renderer->mesh_path(), mesh->mesh_guid))
                    {
                        error = "Cannot resolve mesh resource identity";
                        return rollback();
                    }
                    ++reused_mesh_count;
                }
                else
                {
                    const auto staged_mesh = transaction.staging / (name + ".mesh");
                    fs::path mesh_path;
                    if (!export_path(resource_root / "meshes", name, ".mesh", mesh_path))
                    {
                        return rollback();
                    }
                    if (!matches_saved_source(mesh_path, scene_id, error))
                    {
                        return rollback();
                    }
                    const auto cached = meshes.find(mesh_path);
                    MeshSnapshot snapshot{scene_id, renderer->mesh_instance_id(),
                                          renderer->mesh_revision()};
                    const bool reused =
                        cached != meshes.end() && cached->second.scene_id == scene_id &&
                        cached->second.instance_id == snapshot.instance_id &&
                        cached->second.revision == snapshot.revision;
                    bool changed = false;
                    if (reused)
                    {
                        std::string meta_hash;
                        if (!resolve_guid(mesh_path, "mesh", mesh->mesh_guid, meta_hash, changed))
                        {
                            return rollback();
                        }
                        ++reused_mesh_count;
                    }
                    else
                    {
                        if (!writer || !writer(*node, staged_mesh, error) ||
                            !publish(staged_mesh, mesh_path, "mesh", mesh->mesh_guid, changed))
                        {
                            if (error.empty())
                            {
                                error = "No editable mesh writer is available";
                            }
                            return rollback();
                        }
                        mesh_count += changed ? 1 : 0;
                        reused_mesh_count += changed ? 0 : 1;
                    }
                    saved_meshes[mesh_path] = std::move(snapshot);
                }
                mesh->additional_material_guids.clear();
                for (std::size_t slot = 0; slot < renderer->material_count(); ++slot)
                {
                    const auto* material = renderer->material(slot);
                    Guid material_guid;
                    if (!material->has_overrides())
                    {
                        if (!ResourceSystem::path_to_guid(material->type_path(), material_guid))
                        {
                            error = "Cannot resolve material resource identity";
                            return rollback();
                        }
                        ++reused_material_count;
                    }
                    else
                    {
                        const auto material_name = name + "-slot-" + std::to_string(slot);
                        const auto staged_material =
                            transaction.staging / (material_name + ".material");
                        fs::path material_path;
                        bool changed = false;
                        if (!export_path(resource_root / "materials", material_name, ".material",
                                         material_path) ||
                            !matches_saved_source(material_path, scene_id, error) ||
                            !write_material(*material, staged_material, error) ||
                            !publish(staged_material, material_path, "material", material_guid,
                                     changed))
                        {
                            return rollback();
                        }
                        material_count += changed ? 1 : 0;
                        reused_material_count += changed ? 0 : 1;
                    }
                    if (slot == 0)
                    {
                        mesh->material_guid = std::move(material_guid);
                    }
                    else
                    {
                        mesh->additional_material_guids.push_back(std::move(material_guid));
                    }
                }
            }
            saved->components.push_back(std::move(value));
        }
        data.nodes.push_back(std::move(saved));
    }
    const auto staged_scene = transaction.staging / "saved.scene";
    Guid guid;
    bool scene_changed = false;
    if (!SceneImporter::write(staged_scene, data, error) ||
        !publish(staged_scene, destination, "scene", guid, scene_changed))
    {
        return rollback();
    }
    meshes = std::move(saved_meshes);
    if (saved_scene_id != scene_id)
    {
        sources.clear();
        saved_scene_id = scene_id;
    }
    for (auto& [path, saved_source] : saved_sources)
    {
        sources.insert_or_assign(path, std::move(saved_source));
    }
    result = {{"sceneId", identity},
              {"path", "project/" + relative},
              {"guid", guid},
              {"nodeCount", data.nodes.size()},
              {"resourceCount", resource_count},
              {"meshCount", mesh_count},
              {"materialCount", material_count},
              {"reusedMeshCount", reused_mesh_count},
              {"reusedMaterialCount", reused_material_count},
              {"sceneChanged", scene_changed},
              {"importRequired", resource_count != 0}};
    return true;
}

ScenePersistence::ScenePersistence(WorldSystem& world, fs::path project_root,
                                   MeshSourceWriter writer, SavedCallback saved)
    : state_(std::make_unique<State>()), world_(world), project_root_(std::move(project_root)),
      writer_(std::move(writer)), saved_(std::move(saved))
{
}

ScenePersistence::~ScenePersistence() noexcept = default;

bool ScenePersistence::register_commands(EditorCommands& commands)
{
    return commands.register_command(
        "save_scene", false,
        [this](const Json& arguments, Json& result, std::unique_ptr<EditorEdit>&,
               std::string& error)
        {
            std::vector<SceneSavedResource> changes;
            if (!state_->save(world_, project_root_, writer_, arguments, result, changes, error))
            {
                return false;
            }
            if (saved_)
            {
                saved_(result["path"].get_ref<const std::string&>(), changes);
            }
            return true;
        });
}

} // namespace engine::editor
