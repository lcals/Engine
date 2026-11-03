#include "editor/render/render_component_commands.hpp"

#include "editor/commands/components/component_edit.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/light_probe_volume_component.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/planar_reflection_component.hpp"
#include "runtime/scene/transform.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace engine::editor
{
namespace
{
using Json = nlohmann::json;

std::string configured_path(std::string_view guid)
{
    std::string path;
    if (!guid.empty())
    {
        (void)ResourceSystem::guid_to_path(guid, path);
    }
    return path;
}

bool retain_resource(std::string_view guid, std::vector<ResourcePtr>& versions, std::string& error)
{
    if (guid.empty())
    {
        return true;
    }
    auto* resources = ResourceSystem::instance();
    ResourcePtr resource;
    if (!resources || !resources->loader().load(guid, resource))
    {
        error = "Cannot retain the configured resource version";
        return false;
    }
    versions.push_back(std::move(resource));
    return true;
}

bool read_enabled(const Json& arguments, schema::ComponentDataT& data, std::string& error)
{
    if (!arguments.contains("enabled"))
    {
        return true;
    }
    if (!arguments["enabled"].is_boolean())
    {
        error = "enabled must be boolean";
        return false;
    }
    data.enabled = arguments["enabled"].get<bool>();
    return true;
}

bool prepare_planar(WorldSystem& world, const Json& arguments, Json& result,
                    std::unique_ptr<EditorEdit>& edit, std::string& error)
{
    if (!components::fields(arguments, {"enabled", "resolutionScale", "clipBias"}, error) ||
        arguments.size() <= 2)
    {
        error = "set_planar_reflection requires at least one reflection field";
        return false;
    }
    auto* node = components::target(world, arguments, error);
    if (!node || !node->get_component<MeshRenderer>())
    {
        if (error.empty())
        {
            error = "Planar reflection requires an existing MeshRenderer";
        }
        return false;
    }
    const auto* component = node->get_component<PlanarReflectionComponent>();
    std::optional<schema::ComponentDataT> before;
    schema::ComponentDataT after;
    after.payload.Set(PlanarReflectionSettings{});
    if (component)
    {
        before = component->data();
        after = *before;
    }
    auto& settings = *after.payload.AsPlanarReflectionData();
    if (!read_enabled(arguments, after, error) ||
        (arguments.contains("resolutionScale") &&
         !components::number(arguments["resolutionScale"], settings.resolution_scale)) ||
        (arguments.contains("clipBias") &&
         !components::number(arguments["clipBias"], settings.clip_bias)))
    {
        if (error.empty())
        {
            error = "Reflection fields require finite floats";
        }
        return false;
    }
    result = {{"sceneId", std::to_string(node->scene().id())},
              {"nodeId", std::to_string(node->id())}};
    edit = std::make_unique<components::ComponentEdit>(
        world, node->scene().id(), std::make_shared<std::uint64_t>(node->id()), std::move(before),
        std::move(after), std::vector<ResourcePtr>{}, ComponentType::MeshRendererData);
    return true;
}

bool prepare_probe(WorldSystem& world, const Json& arguments, Json& result,
                   std::unique_ptr<EditorEdit>& edit, std::string& error)
{
    if (!components::fields(arguments, {"enabled", "resource"}, error) || arguments.size() <= 2)
    {
        error = "set_light_probe_volume requires at least one probe field";
        return false;
    }
    auto* node = components::target(world, arguments, error);
    if (!node)
    {
        return false;
    }
    const auto* component = node->get_component<LightProbeVolumeComponent>();
    std::optional<schema::ComponentDataT> before;
    schema::ComponentDataT after;
    after.payload.Set(LightProbeVolumeSettings{});
    std::vector<ResourcePtr> versions;
    if (component)
    {
        before = component->data();
        after = *before;
        if (!retain_resource(component->settings().probes_guid, versions, error))
        {
            return false;
        }
    }
    if (!read_enabled(arguments, after, error))
    {
        return false;
    }
    auto& settings = *after.payload.AsLightProbeVolumeData();
    if (arguments.contains("resource"))
    {
        if (!arguments["resource"].is_string())
        {
            error = "resource must be a logical resource path or an empty string";
            return false;
        }
        const auto path = arguments["resource"].get<std::string>();
        settings.probes_guid.clear();
        if (!path.empty() &&
            (!ResourceSystem::path_to_guid(path, settings.probes_guid) ||
             !retain_resource(settings.probes_guid, versions, error) ||
             !versions.back()->light_probe()))
        {
            error = "resource must identify a light-probe resource in the current catalog";
            return false;
        }
    }
    result = {{"sceneId", std::to_string(node->scene().id())},
              {"nodeId", std::to_string(node->id())}};
    edit = std::make_unique<components::ComponentEdit>(
        world, node->scene().id(), std::make_shared<std::uint64_t>(node->id()), std::move(before),
        std::move(after), std::move(versions));
    return true;
}

// 只修改照明字段，不以 restore_data 重建网格或丢弃其材质覆盖和瞬态变形。
class MeshLightingEdit final : public EditorEdit
{
public:
    MeshLightingEdit(WorldSystem& world, const MeshRenderer& mesh, Json arguments)
        : world_(world), scene_id_(mesh.node().scene().id()), node_id_(mesh.node().id()),
          instance_(mesh.mesh_instance_id()), revision_(mesh.mesh_revision()),
          before_(mesh.data()), after_(before_), arguments_(std::move(arguments))
    {
    }

    bool prepare(const MeshRenderer& mesh, std::string& error)
    {
        auto* settings = after_.payload.AsMeshRendererData();
        for (const auto* name : {"castShadows", "bakeStatic"})
        {
            if (!arguments_.contains(name))
            {
                continue;
            }
            if (!arguments_[name].is_boolean())
            {
                error = "Mesh lighting flags must be boolean";
                return false;
            }
            if (std::string_view(name) == "castShadows")
            {
                settings->cast_shadows = arguments_[name].get<bool>();
            }
            else
            {
                settings->bake_static = arguments_[name].get<bool>();
            }
        }
        if (!arguments_.contains("lightmap"))
        {
            return true;
        }
        if (!arguments_["lightmap"].is_string())
        {
            error = "lightmap must be a logical texture path or an empty string";
            return false;
        }
        if (!settings->lightmap_guid.empty() && !mesh.lightmap_resource())
        {
            error = "The configured lightmap is stale; restore its baked mesh/transform "
                    "before an undoable replacement";
            return false;
        }
        const auto* transform = mesh.node().get_component<Transform>();
        if (!transform || !transform->world_matrix(transform_.data()))
        {
            error = "Mesh lightmap editing requires a valid Transform";
            return false;
        }
        before_path_ = configured_path(settings->lightmap_guid);
        after_path_ = arguments_["lightmap"].get<std::string>();
        Guid requested;
        if ((!after_path_.empty() && !ResourceSystem::path_to_guid(after_path_, requested)) ||
            !retain_resource(settings->mesh_guid, versions_, error) ||
            !retain_resource(settings->lightmap_guid, versions_, error) ||
            !retain_resource(requested, versions_, error))
        {
            if (error.empty())
            {
                error = "Unknown lightmap resource path";
            }
            return false;
        }
        return true;
    }

    bool apply(std::string& error) override
    {
        return change(before_, after_, after_path_, true, error);
    }
    bool revert(std::string& error) override
    {
        return change(after_, before_, before_path_, false, error);
    }

private:
    bool change(const schema::ComponentDataT& expected, schema::ComponentDataT& replacement,
                const std::string& path, bool applying, std::string& error)
    {
        auto* scene = world_.scene();
        auto* node = scene && scene->id() == scene_id_ ? scene->find_node(node_id_) : nullptr;
        auto* mesh = node ? node->get_component<MeshRenderer>() : nullptr;
        if (!mesh || mesh->mesh_instance_id() != instance_ || mesh->mesh_revision() != revision_ ||
            mesh->data() != expected)
        {
            error = "Mesh or lighting settings changed outside this edit";
            return false;
        }
        if (arguments_.contains("lightmap"))
        {
            const auto* transform = node->get_component<Transform>();
            std::array<float, 16> matrix{};
            if (!transform || !transform->world_matrix(matrix.data()) || matrix != transform_)
            {
                error = "The lightmap mesh transform changed outside this edit";
                return false;
            }
            const auto* resources = ResourceSystem::instance();
            for (const auto& resource : versions_)
            {
                const auto* available =
                    resources ? resources->catalog().find(resource->info().guid) : nullptr;
                if (!available || *available != resource->info())
                {
                    error = "Mesh or lightmap resource version changed outside this edit";
                    return false;
                }
            }
        }
        const auto* desired = replacement.payload.AsMeshRendererData();
        const bool previous_static = mesh->bake_static();
        if (arguments_.contains("bakeStatic") && !mesh->set_bake_static(desired->bake_static))
        {
            error = "A skinned or deformed mesh cannot be marked bakeStatic";
            return false;
        }
        if (arguments_.contains("lightmap") && !mesh->set_lightmap(path))
        {
            const bool restored = mesh->set_bake_static(previous_static);
            error = restored
                        ? "Lightmap requires a resource mesh with UV2, no deformation, "
                          "and a linear 2D texture"
                        : "Cannot restore the original bakeStatic flag";
            return false;
        }
        if (arguments_.contains("castShadows"))
        {
            mesh->set_cast_shadows(desired->cast_shadows);
        }
        if (applying)
        {
            replacement = mesh->data();
        }
        return true;
    }

    WorldSystem& world_;
    std::uint64_t scene_id_;
    std::uint64_t node_id_;
    std::uint64_t instance_;
    std::uint64_t revision_;
    schema::ComponentDataT before_;
    schema::ComponentDataT after_;
    Json arguments_;
    std::string before_path_;
    std::string after_path_;
    std::array<float, 16> transform_{};
    std::vector<ResourcePtr> versions_;
};

bool prepare_mesh(WorldSystem& world, const Json& arguments, Json& result,
                  std::unique_ptr<EditorEdit>& edit, std::string& error)
{
    if (!components::fields(arguments, {"castShadows", "bakeStatic", "lightmap"}, error) ||
        arguments.size() <= 2)
    {
        error = "set_mesh_lighting requires at least one mesh lighting field";
        return false;
    }
    auto* node = components::target(world, arguments, error);
    const auto* mesh = node ? node->get_component<MeshRenderer>() : nullptr;
    if (!mesh)
    {
        if (error.empty())
        {
            error = "Node has no MeshRenderer";
        }
        return false;
    }
    auto pending = std::make_unique<MeshLightingEdit>(world, *mesh, arguments);
    if (!pending->prepare(*mesh, error))
    {
        return false;
    }
    result = {{"sceneId", std::to_string(node->scene().id())},
              {"nodeId", std::to_string(node->id())}};
    edit = std::move(pending);
    return true;
}
} // namespace

nlohmann::json planar_reflection_json(const PlanarReflectionComponent& component)
{
    return {{"enabled", component.enabled()},
            {"resolutionScale", component.settings().resolution_scale},
            {"clipBias", component.settings().clip_bias}};
}

nlohmann::json light_probe_volume_json(const LightProbeVolumeComponent& component)
{
    return {{"enabled", component.enabled()},
            {"resource", configured_path(component.settings().probes_guid)},
            {"resourceGuid", component.settings().probes_guid},
            {"valid", bool(component.resource())}};
}

nlohmann::json mesh_lighting_json(const MeshRenderer& component)
{
    const auto& settings = *component.data().payload.AsMeshRendererData();
    return {{"castShadows", component.cast_shadows()},
            {"bakeStatic", component.bake_static()},
            {"lightmap", configured_path(settings.lightmap_guid)},
            {"lightmapGuid", settings.lightmap_guid},
            {"lightmapValid", bool(component.lightmap_resource())}};
}

bool register_render_component_commands(WorldSystem& world, EditorCommands& commands)
{
    const auto add_get = [&world, &commands]<typename T>(const char* name, auto write)
    {
        return commands.register_command(name, false,
            [&world, write](const Json& arguments, Json& result, std::unique_ptr<EditorEdit>&,
                            std::string& error)
            {
                if (!components::fields(arguments, {}, error))
                {
                    return false;
                }
                const auto* node = components::target(world, arguments, error);
                const auto* component = node ? node->get_component<T>() : nullptr;
                if (!component)
                {
                    if (error.empty())
                    {
                        error = "Requested rendering component does not exist";
                    }
                    return false;
                }
                result = write(*component);
                result["sceneId"] = std::to_string(node->scene().id());
                result["nodeId"] = std::to_string(node->id());
                return true;
            });
    };
    const auto add_set = [&world, &commands](const char* name, auto prepare)
    {
        return commands.register_command(name, true,
            [&world, prepare](const Json& arguments, Json& result, std::unique_ptr<EditorEdit>& edit,
                              std::string& error)
            { return prepare(world, arguments, result, edit, error); });
    };
    return add_get.operator()<PlanarReflectionComponent>("get_planar_reflection",
                                                         planar_reflection_json) &&
           add_set("set_planar_reflection", prepare_planar) &&
           add_get.operator()<LightProbeVolumeComponent>("get_light_probe_volume",
                                                         light_probe_volume_json) &&
           add_set("set_light_probe_volume", prepare_probe) &&
           add_get.operator()<MeshRenderer>("get_mesh_lighting", mesh_lighting_json) &&
           add_set("set_mesh_lighting", prepare_mesh);
}
} // namespace engine::editor
