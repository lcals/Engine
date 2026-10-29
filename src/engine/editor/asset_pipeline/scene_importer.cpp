#include "editor/asset_pipeline/scene_importer.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/toml_helpers.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <sstream>

namespace engine
{
namespace
{

bool validate(const schema::SceneDataT& scene, std::string& error)
{
    if (scene.schema_version != 1 || scene.info == nullptr || scene.info->name.empty())
    {
        error = "scene requires version 1 and a name";
        return false;
    }
    std::map<std::uint64_t, const schema::NodeDataT*> nodes;
    for (const auto& node : scene.nodes)
    {
        if (node == nullptr || node->info == nullptr || node->info->id == 0 ||
            !nodes.emplace(node->info->id, node.get()).second)
        {
            error = "scene node IDs must be unique and nonzero";
            return false;
        }
        std::set<schema::ComponentPayload> components;
        for (const auto& component : node->components)
        {
            if (component == nullptr || !components.insert(component->payload.type).second)
            {
                error = "scene node components must be unique";
                return false;
            }
            switch (component->payload.type)
            {
            case schema::ComponentPayload::TransformData:
            {
                const auto* value = component->payload.AsTransformData();
                const auto finite = [](const schema::Vec3& vector)
                {
                    return std::isfinite(vector.x()) && std::isfinite(vector.y()) &&
                           std::isfinite(vector.z());
                };
                if (value == nullptr || !finite(value->position) || !finite(value->rotation) ||
                    !finite(value->scale) || value->scale.x() == 0 || value->scale.y() == 0 ||
                    value->scale.z() == 0)
                {
                    error = "invalid scene transform";
                    return false;
                }
                break;
            }
            case schema::ComponentPayload::CameraData:
            {
                const auto* value = component->payload.AsCameraData();
                if (value == nullptr || !std::isfinite(value->fov_degrees) ||
                    !std::isfinite(value->near_clip) || !std::isfinite(value->far_clip) ||
                    value->fov_degrees <= 0 || value->fov_degrees >= 180 || value->near_clip <= 0 ||
                    value->far_clip <= value->near_clip)
                {
                    error = "invalid scene camera";
                    return false;
                }
                break;
            }
            case schema::ComponentPayload::LightData:
            {
                const auto* value = component->payload.AsLightData();
                if (value == nullptr || !std::isfinite(value->intensity) || value->intensity < 0 ||
                    !std::isfinite(value->color.r()) || !std::isfinite(value->color.g()) ||
                    !std::isfinite(value->color.b()) || !std::isfinite(value->color.a()) ||
                    value->color.r() < 0 || value->color.g() < 0 || value->color.b() < 0 ||
                    value->color.a() < 0 || value->color.a() > 1 || value->color.r() > 1 ||
                    value->color.g() > 1 || value->color.b() > 1)
                {
                    error = "invalid scene light";
                    return false;
                }
                break;
            }
            case schema::ComponentPayload::MeshRendererData:
            {
                const auto* value = component->payload.AsMeshRendererData();
                if (value == nullptr || !GuidMap::valid_guid(value->mesh_guid) ||
                    !GuidMap::valid_guid(value->material_guid))
                {
                    error = "mesh renderer requires mesh and material GUIDs";
                    return false;
                }
                break;
            }
            default:
                error = "unknown scene component";
                return false;
            }
        }
        if (!components.contains(schema::ComponentPayload::TransformData))
        {
            error = "each scene node requires a Transform component";
            return false;
        }
    }
    for (const auto& [id, node] : nodes)
    {
        std::set<std::uint64_t> ancestors{id};
        auto parent = node->info->parent_id;
        while (parent != 0)
        {
            const auto iterator = nodes.find(parent);
            if (iterator == nodes.end() || !ancestors.insert(parent).second)
            {
                error = "scene parent is missing or hierarchy has a cycle";
                return false;
            }
            parent = iterator->second->info->parent_id;
        }
    }
    if (scene.info->active_camera_node != 0)
    {
        const auto camera = nodes.find(scene.info->active_camera_node);
        if (camera == nodes.end() ||
            std::none_of(camera->second->components.begin(), camera->second->components.end(),
                         [](const auto& component)
                         {
                             return component->enabled &&
                                    component->payload.type == schema::ComponentPayload::CameraData;
                         }))
        {
            error = "active camera must refer to a node with an enabled Camera component";
            return false;
        }
    }
    return true;
}

bool read_component(const toml::table& table, schema::ComponentDataT& component, std::string& error)
{
    using namespace asset_pipeline;
    component.enabled = table["enabled"].value_or(true);
    if (table.contains("enabled") && !table["enabled"].is_boolean())
    {
        error = "component enabled must be boolean";
        return false;
    }
    const auto type = table["type"].value<std::string_view>();
    if (type == "transform")
    {
        std::array<double, 3> position{0, 0, 0};
        std::array<double, 3> rotation{0, 0, 0};
        std::array<double, 3> scale{1, 1, 1};
        if ((table.contains("position") && !numbers(table.get("position"), position)) ||
            (table.contains("rotation") && !numbers(table.get("rotation"), rotation)) ||
            (table.contains("scale") && !numbers(table.get("scale"), scale)))
        {
            error = "transform values must be finite three-element arrays";
            return false;
        }
        schema::TransformDataT value;
        value.position = {position[0], position[1], position[2]};
        value.rotation = {rotation[0], rotation[1], rotation[2]};
        value.scale = {scale[0], scale[1], scale[2]};
        component.payload.Set(std::move(value));
    }
    else if (type == "camera")
    {
        schema::CameraDataT value;
        if ((table.contains("fov_degrees") &&
             !float_number(table.get("fov_degrees"), value.fov_degrees)) ||
            (table.contains("near_clip") &&
             !float_number(table.get("near_clip"), value.near_clip)) ||
            (table.contains("far_clip") && !float_number(table.get("far_clip"), value.far_clip)))
        {
            error = "camera fields must be finite numbers";
            return false;
        }
        component.payload.Set(std::move(value));
    }
    else if (type == "light")
    {
        schema::LightDataT value;
        std::array<double, 4> color{1, 1, 1, 1};
        if ((table.contains("color") && !numbers(table.get("color"), color)) ||
            (table.contains("intensity") && !float_number(table.get("intensity"), value.intensity)))
        {
            error = "light color and intensity must be finite numbers";
            return false;
        }
        for (const auto channel : color)
        {
            if (channel < 0 || channel > std::numeric_limits<float>::max())
            {
                error = "light color channel is out of range";
                return false;
            }
        }
        value.color = {static_cast<float>(color[0]), static_cast<float>(color[1]),
                       static_cast<float>(color[2]), static_cast<float>(color[3])};
        component.payload.Set(std::move(value));
    }
    else if (type == "mesh_renderer")
    {
        schema::MeshRendererDataT value;
        value.mesh_guid = table["mesh"].value_or(std::string{});
        value.material_guid = table["material"].value_or(std::string{});
        component.payload.Set(std::move(value));
    }
    else
    {
        error = "unknown scene component type";
        return false;
    }
    return true;
}

} // namespace

bool SceneImporter::read(const std::filesystem::path& source, schema::SceneDataT& output,
                         std::string& error)
{
    output = {};
    error.clear();
    std::string text;
    if (!ResourceSystem::read_text(source, text))
    {
        error = "cannot read scene: " + PathUtils::to_utf8(source);
        return false;
    }
    auto parsed = toml::parse(text);
    if (!parsed)
    {
        error = "invalid scene TOML: " + std::string(parsed.error().description());
        return false;
    }
    const auto& table = parsed.table();
    schema::SceneDataT data;
    data.info = std::make_unique<schema::SceneInfoT>();
    data.info->name = table["name"].value_or(std::string{});
    if (table.contains("active_camera") &&
        !asset_pipeline::identifier(table.get("active_camera"), data.info->active_camera_node))
    {
        error = "active_camera must be an unsigned decimal ID";
        return false;
    }
    const auto* nodes = table["nodes"].as_array();
    if (nodes == nullptr)
    {
        error = "scene requires nodes array";
        return false;
    }
    for (const auto& item : *nodes)
    {
        const auto* node_table = item.as_table();
        auto node = std::make_unique<schema::NodeDataT>();
        node->info = std::make_unique<schema::NodeInfoT>();
        if (node_table == nullptr ||
            !asset_pipeline::identifier(node_table->get("id"), node->info->id) ||
            (node_table->contains("parent") &&
             !asset_pipeline::identifier(node_table->get("parent"), node->info->parent_id)))
        {
            error = "node id/parent must be unsigned decimal IDs";
            return false;
        }
        node->info->name = (*node_table)["name"].value_or(std::string{});
        const auto* components = (*node_table)["components"].as_array();
        if (components == nullptr)
        {
            error = "node requires components array";
            return false;
        }
        for (const auto& item_component : *components)
        {
            const auto* component_table = item_component.as_table();
            auto component = std::make_unique<schema::ComponentDataT>();
            if (component_table == nullptr || !read_component(*component_table, *component, error))
            {
                if (error.empty())
                {
                    error = "component must be a table";
                }
                return false;
            }
            node->components.push_back(std::move(component));
        }
        data.nodes.push_back(std::move(node));
    }
    if (!validate(data, error))
    {
        return false;
    }
    output = std::move(data);
    return true;
}

bool SceneImporter::write(const std::filesystem::path& destination, const schema::SceneDataT& data,
                          std::string& error)
{
    error.clear();
    if (!validate(data, error))
    {
        return false;
    }
    toml::table document{{"name", data.info->name},
                         {"active_camera", std::to_string(data.info->active_camera_node)}};
    toml::array nodes;
    for (const auto& node : data.nodes)
    {
        toml::table table{{"id", std::to_string(node->info->id)},
                          {"name", node->info->name},
                          {"parent", std::to_string(node->info->parent_id)}};
        toml::array components;
        for (const auto& component : node->components)
        {
            toml::table value{{"enabled", component->enabled}};
            switch (component->payload.type)
            {
            case schema::ComponentPayload::TransformData:
            {
                const auto* transform = component->payload.AsTransformData();
                value.insert("type", "transform");
                value.insert("position",
                             toml::array{transform->position.x(), transform->position.y(),
                                         transform->position.z()});
                value.insert("rotation",
                             toml::array{transform->rotation.x(), transform->rotation.y(),
                                         transform->rotation.z()});
                value.insert("scale", toml::array{transform->scale.x(), transform->scale.y(),
                                                  transform->scale.z()});
                break;
            }
            case schema::ComponentPayload::CameraData:
            {
                const auto* camera = component->payload.AsCameraData();
                value.insert("type", "camera");
                value.insert("fov_degrees", camera->fov_degrees);
                value.insert("near_clip", camera->near_clip);
                value.insert("far_clip", camera->far_clip);
                break;
            }
            case schema::ComponentPayload::LightData:
            {
                const auto* light = component->payload.AsLightData();
                value.insert("type", "light");
                value.insert("color", toml::array{light->color.r(), light->color.g(),
                                                  light->color.b(), light->color.a()});
                value.insert("intensity", light->intensity);
                break;
            }
            case schema::ComponentPayload::MeshRendererData:
            {
                const auto* mesh = component->payload.AsMeshRendererData();
                value.insert("type", "mesh_renderer");
                value.insert("mesh", mesh->mesh_guid);
                value.insert("material", mesh->material_guid);
                break;
            }
            default:
                return false;
            }
            components.push_back(std::move(value));
        }
        table.insert("components", std::move(components));
        nodes.push_back(std::move(table));
    }
    document.insert("nodes", std::move(nodes));
    std::ostringstream stream;
    stream << document;
    auto temporary = destination;
    temporary += ".saving-" + asset_pipeline::new_guid();
    if (!asset_pipeline::write_text(temporary, stream.str(), error))
    {
        return false;
    }
    std::error_code filesystem_error;
    // POSIX rename 替换已有目标；Windows 以同目录备份保护旧内容。
    auto backup = temporary;
    backup += ".previous";
    const bool existed = std::filesystem::exists(destination, filesystem_error);
    if (!filesystem_error && existed)
    {
        std::filesystem::rename(destination, backup, filesystem_error);
    }
    if (!filesystem_error)
    {
        std::filesystem::rename(temporary, destination, filesystem_error);
    }
    if (filesystem_error)
    {
        error = "cannot publish scene: " + filesystem_error.message();
        std::error_code cleanup_error;
        if (std::filesystem::exists(backup, cleanup_error))
        {
            std::filesystem::rename(backup, destination, cleanup_error);
        }
        std::filesystem::remove(temporary, cleanup_error);
        return false;
    }
    std::filesystem::remove(backup, filesystem_error);
    return true;
}

} // namespace engine
