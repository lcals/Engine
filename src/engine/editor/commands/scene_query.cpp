#include "editor/commands/scene_query.hpp"

#include "editor/commands/components/component_edit.hpp"
#include "editor/render/render_component_commands.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/light_probe_volume_component.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/planar_reflection_component.hpp"
#include "runtime/scene/post_process_component.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/scene/ui_component.hpp"

#include <algorithm>

namespace engine::editor
{
using components::fields;
using components::Json;
using components::json_vector;

bool query_scene(WorldSystem& world, const Json& arguments, Json& result, std::string& error)
{
    if (!fields(arguments, {"offset", "limit"}))
    {
        error = "get_scene accepts only offset and limit";
        return false;
    }
    const auto read_count = [&arguments](const char* name, std::size_t& output)
    {
        const auto found = arguments.find(name);
        if (found == arguments.end())
        {
            return true;
        }
        if (!found->is_number_integer() ||
            (!found->is_number_unsigned() && found->get<std::int64_t>() < 0))
        {
            return false;
        }
        const auto value = found->get<std::uint64_t>();
        if (value > std::numeric_limits<std::size_t>::max())
        {
            return false;
        }
        output = static_cast<std::size_t>(value);
        return true;
    };
    std::size_t offset = 0;
    std::size_t limit = std::numeric_limits<std::size_t>::max();
    if (!read_count("offset", offset) || !read_count("limit", limit))
    {
        error = "get_scene offset and limit must be nonnegative integers in platform size range";
        return false;
    }
    const auto* scene = world.scene();
    result = {{"scene", nullptr}, {"totalNodes", 0}, {"offset", offset}};
    if (scene == nullptr)
    {
        return true;
    }
    std::vector<Node> scene_nodes;
    if (!scene->collect_nodes(scene_nodes))
    {
        error = "Cannot collect scene entities";
        return false;
    }
    result["totalNodes"] = scene_nodes.size();
    const auto start = std::min(offset, scene_nodes.size());
    const auto end = start + std::min(limit, scene_nodes.size() - start);
    Json nodes = Json::array();
    for (std::size_t index = start; index < end; ++index)
    {
        const auto& node = scene_nodes[index];
        Json components = Json::array();
        std::vector<Component> component_handles;
        if (!node.collect_components(component_handles))
        {
            error = "Cannot collect entity components";
            return false;
        }
        for (const auto& component : component_handles)
        {
            Json data = {{"enabled", component.enabled()},
                         {"active", component.active()},
                         {"visible", component.visible()},
                         {"visibleInHierarchy", component.visible_in_hierarchy()},
                         {"componentType", schema::EnumNameComponentPayload(component.type())},
                         {"instanceId", std::to_string(component.instance_id())}};
            switch (component.type())
            {
            case ComponentType::TransformData:
            {
                const auto& value = node.get_component<Transform>().settings();
                data.update({{"type", "transform"},
                             {"position", json_vector(value.position)},
                             {"rotation", json_vector(value.rotation)},
                             {"scale", json_vector(value.scale)}});
                break;
            }
            case ComponentType::CameraData:
            {
                const auto& value = node.get_component<Camera>().settings();
                data.update({{"type", "camera"},
                             {"fovDegrees", value.fov_degrees},
                             {"nearClip", value.near_clip},
                             {"farClip", value.far_clip},
                             {"projection", schema::EnumNameProjection(value.projection)},
                             {"orthographicSize", value.orthographic_size},
                             {"frustumCullingEnabled", value.frustum_culling_enabled}});
                break;
            }
            case ComponentType::LightData:
            {
                const auto& value = node.get_component<Light>().settings();
                data.update({{"type", "light"},
                             {"color",
                              {value.color.r(), value.color.g(), value.color.b(), value.color.a()}},
                             {"intensity", value.intensity},
                             {"kind", schema::EnumNameLightKind(value.kind)},
                             {"range", value.range},
                             {"innerAngle", value.inner_angle},
                             {"outerAngle", value.outer_angle},
                             {"castShadows", value.cast_shadows},
                             {"shadowResolution", value.shadow_resolution},
                             {"shadowBias", value.shadow_bias}});
                break;
            }
            case ComponentType::MeshRendererData:
            {
                const auto& value = node.get_component<MeshRenderer>();
                Json materials = Json::array();
                for (std::size_t slot = 0; slot < value.material_count(); ++slot)
                {
                    materials.push_back(components::material_snapshot(value.material(slot)));
                }
                data["materials"] = std::move(materials);
                data.update(mesh_lighting_json(value));
                data.update({{"type", "mesh_renderer"},
                             {"meshPath", value.mesh_path()},
                             {"staticInstancingEnabled", value.static_instancing_enabled()},
                             {"staticLodEnabled", value.static_lod_enabled()},
                             {"castShadows", value.cast_shadows()},
                             {"material", components::material_snapshot(value.material())}});
                break;
            }
            case ComponentType::PlanarReflectionData:
                data.update(
                    planar_reflection_json(node.get_component<PlanarReflectionComponent>()));
                data["type"] = "planar_reflection";
                break;
            case ComponentType::LightProbeVolumeData:
                data.update(
                    light_probe_volume_json(node.get_component<LightProbeVolumeComponent>()));
                data["type"] = "light_probe_volume";
                break;
            case ComponentType::UiData:
            {
                const auto& value = node.get_component<UiComponent>();
                const auto& guid = value.settings().document_guid;
                std::string path;
                if (!guid.empty() && !ResourceSystem::guid_to_path(guid, path))
                {
                    path.clear();
                }
                data.update({{"type", "ui"}, {"document", path}, {"documentGuid", guid}});
                break;
            }
            case ComponentType::PostProcessData:
            {
                const auto& value = node.get_component<PostProcessComponent>().settings();
                data.update({{"type", "post_process"},
                             {"ssrEnabled", value.ssr_enabled},
                             {"ssrResolutionScale", value.ssr_resolution_scale},
                             {"ssrMaxSteps", value.ssr_max_steps},
                             {"ssrMaxDistance", value.ssr_max_distance},
                             {"ssrThickness", value.ssr_thickness},
                             {"ssrRoughnessCutoff", value.ssr_roughness_cutoff}});
                break;
            }
            default:
                data["type"] = schema::EnumNameComponentPayload(component.type());
                break;
            }
            components.push_back(std::move(data));
        }
        nodes.push_back({{"nodeId", std::to_string(node.id())},
                         {"name", node.name()},
                         {"enabled", node.enabled()},
                         {"active", node.active()},
                         {"visible", node.visible()},
                         {"visibleInHierarchy", node.visible_in_hierarchy()},
                         {"parentId", !node.parent() ? Json(nullptr)
                                                     : Json(std::to_string(node.parent().id()))},
                         {"components", std::move(components)}});
    }
    const auto camera = scene->active_camera();
    result["scene"] = {
        {"sceneId", std::to_string(scene->id())},
        {"name", scene->name()},
        {"activeCameraNodeId", !camera ? Json(nullptr) : Json(std::to_string(camera.node().id()))},
        {"nodes", std::move(nodes)}};
    return true;
}
} // namespace engine::editor
