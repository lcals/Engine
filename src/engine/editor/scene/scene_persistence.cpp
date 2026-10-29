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
#include <memory>
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
        files.push_back(std::move(file));
        return true;
    }

    bool publish(const fs::path& temporary, const fs::path& path, std::string& error)
    {
        auto meta = path;
        meta += ".meta";
        if (!remember(path, error) || !remember(meta, error))
        {
            return false;
        }
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        if (ec || !SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(),
                                  PathUtils::to_utf8(path).c_str()))
        {
            error = "Cannot publish scene resource";
            return false;
        }
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

bool save_scene(WorldSystem& world, const fs::path& project_root,
                const ScenePersistence::MeshSourceWriter& writer, const Json& arguments,
                Json& result, std::string& error)
{
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
    SourceTransaction transaction;
    transaction.staging = root / "cache" / (".scene-save-" + asset_pipeline::new_guid());
    if (!fs::create_directory(transaction.staging, ec) || ec)
    {
        error = "Cannot create scene staging directory";
        return false;
    }
    auto resource_name = destination.stem();
    resource_name += "-resources";
    const auto resource_root = destination.parent_path() / resource_name;
    std::size_t resource_count = 0;
    const auto publish = [&](const fs::path& staged, const fs::path& path,
                             std::string_view importer, std::string& guid)
    {
        if (!transaction.publish(staged, path, error) ||
            !asset_pipeline::ensure_asset_guid(root, path, importer, guid, error))
        {
            return false;
        }
        ++resource_count;
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
            if (component->type() == ComponentType::MeshRendererData)
            {
                const auto* renderer = node->get_component<MeshRenderer>();
                auto* mesh = value->payload.AsMeshRendererData();
                const auto name = "node-" + std::to_string(node->info().id);
                const auto staged_mesh = transaction.staging / (name + ".mesh");
                const auto mesh_path = resource_root / "meshes" / (name + ".mesh");
                if (!writer || !mesh || !writer(*node, staged_mesh, error) ||
                    !publish(staged_mesh, mesh_path, "mesh", mesh->mesh_guid))
                {
                    if (error.empty())
                    {
                        error = "No editable mesh writer is available";
                    }
                    return rollback();
                }
                mesh->additional_material_guids.clear();
                for (std::size_t slot = 0; slot < renderer->material_count(); ++slot)
                {
                    const auto filename = name + "-slot-" + std::to_string(slot) + ".material";
                    const auto staged_material = transaction.staging / filename;
                    Guid material_guid;
                    if (!write_material(*renderer->material(slot), staged_material, error) ||
                        !publish(staged_material, resource_root / "materials" / filename,
                                 "material", material_guid))
                    {
                        return rollback();
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
    if (!SceneImporter::write(staged_scene, data, error) ||
        !publish(staged_scene, destination, "scene", guid))
    {
        return rollback();
    }
    result = {{"sceneId", identity},
              {"path", "project/" + relative},
              {"guid", guid},
              {"nodeCount", data.nodes.size()},
              {"resourceCount", resource_count},
              {"importRequired", true}};
    return true;
}
} // namespace

ScenePersistence::ScenePersistence(WorldSystem& world, fs::path project_root,
                                   MeshSourceWriter writer)
    : world_(world), project_root_(std::move(project_root)), writer_(std::move(writer))
{
}

bool ScenePersistence::register_commands(EditorCommands& commands)
{
    return commands.register_command(
        "save_scene", false,
        [this](const Json& arguments, Json& result, std::unique_ptr<EditorEdit>&,
               std::string& error)
        { return save_scene(world_, project_root_, writer_, arguments, result, error); });
}

} // namespace engine::editor
