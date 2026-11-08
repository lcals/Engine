#include "editor/commands/scene_commands.hpp"

#include "editor/commands/components/component_edit.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/scene_query.hpp"
#if ENGINE_HAS_ASSET_PIPELINE
#include "editor/commands/prefab_commands.hpp"
#endif
#include "runtime/scene/camera.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/transform.hpp"

#include <algorithm>

namespace engine::editor
{
namespace
{
using components::fields;
using components::identifier;
using components::Json;
using components::NodeAliases;
using components::Target;

std::vector<std::uint8_t> node_snapshot(const Node& node)
{
    schema::NodeDataT data;
    data.info = std::make_unique<schema::NodeInfoT>();
    if (!node.build_info(*data.info))
    {
        return {};
    }
    std::vector<Component> components;
    if (!node.collect_components(components))
    {
        return {};
    }
    for (const auto& component : components)
    {
        auto snapshot = std::make_unique<schema::ComponentDataT>();
        if (!component.get_data(*snapshot))
        {
            return {};
        }
        data.components.push_back(std::move(snapshot));
    }
    flatbuffers::FlatBufferBuilder builder;
    builder.Finish(schema::NodeData::Pack(builder, &data));
    return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

class StateEdit final : public EditorEdit
{
public:
    Target target;
    ComponentType component_type = ComponentType::NONE;
    bool change_enabled = false;
    bool change_visible = false;
    bool before_enabled = true;
    bool before_visible = true;
    bool after_enabled = true;
    bool after_visible = true;

    explicit StateEdit(Target value) : target(std::move(value))
    {
    }

    bool apply(std::string& error) override
    {
        return change(before_enabled, before_visible, after_enabled, after_visible, error);
    }

    bool revert(std::string& error) override
    {
        return change(after_enabled, after_visible, before_enabled, before_visible, error);
    }

private:
    bool change(bool expected_enabled, bool expected_visible, bool enabled, bool visible,
                std::string& error)
    {
        auto node = target.node(error);
        if (!node)
        {
            return false;
        }
        Component component = {};
        if (component_type != ComponentType::NONE)
        {
            component = node.get_component(component_type);
            if (!component)
            {
                error = "The requested component no longer exists";
                return false;
            }
        }
        const bool current_enabled = component ? component.enabled() : node.enabled();
        const bool current_visible = component ? component.visible() : node.visible();
        if ((change_enabled && current_enabled != expected_enabled) ||
            (change_visible && current_visible != expected_visible))
        {
            error = "State flags changed outside this edit; undo or redo is stale";
            return false;
        }
        const bool changed = component ? (!change_enabled || component.set_enabled(enabled)) &&
                                             (!change_visible || component.set_visible(visible))
                                       : (!change_enabled || node.set_enabled(enabled)) &&
                                             (!change_visible || node.set_visible(visible));
        if (!changed)
        {
            error = "Cannot update entity or component state";
            return false;
        }
        return true;
    }
};

bool prepare_state(WorldSystem& world, NodeAliases& aliases, const Json& arguments, Json& result,
                   std::unique_ptr<EditorEdit>& output, std::string& error, bool component_state)
{
    Target target(world, &aliases);
    if (!fields(arguments,
                component_state
                    ? std::initializer_list<std::string_view>{"sceneId", "nodeId", "componentType",
                                                              "enabled", "visible"}
                    : std::initializer_list<std::string_view>{"sceneId", "nodeId", "enabled",
                                                              "visible"}) ||
        (!arguments.contains("enabled") && !arguments.contains("visible")) ||
        (arguments.contains("enabled") && !arguments["enabled"].is_boolean()) ||
        (arguments.contains("visible") && !arguments["visible"].is_boolean()) ||
        !target.read(arguments, error))
    {
        error = "State commands require a scene/node target and enabled or visible booleans";
        return false;
    }
    auto record = std::make_unique<StateEdit>(std::move(target));
    auto node = record->target.node(error);
    record->before_enabled = node.enabled();
    record->before_visible = node.visible();
    if (component_state)
    {
        if (!arguments.contains("componentType") || !arguments["componentType"].is_string())
        {
            error = "componentType must name a componentType returned by get_scene";
            return false;
        }
        std::vector<Component> components;
        if (!node.collect_components(components))
        {
            return {};
        }
        for (const auto& component : components)
        {
            if (arguments["componentType"] == schema::EnumNameComponentPayload(component.type()))
            {
                record->component_type = component.type();
                record->before_enabled = component.enabled();
                record->before_visible = component.visible();
                break;
            }
        }
        if (record->component_type == ComponentType::NONE)
        {
            error = "The requested component type is not present on this node";
            return false;
        }
    }
    record->change_enabled = arguments.contains("enabled");
    record->change_visible = arguments.contains("visible");
    record->after_enabled = arguments.value("enabled", record->before_enabled);
    record->after_visible = arguments.value("visible", record->before_visible);
    result = {{"sceneId", arguments["sceneId"]},
              {"nodeId", arguments["nodeId"]},
              {"enabled", record->after_enabled},
              {"visible", record->after_visible}};
    if (component_state)
    {
        result["componentType"] = arguments["componentType"];
    }
    output = std::move(record);
    return true;
}

class CreateNodeEdit final : public EditorEdit
{
public:
    Target target;
    std::string name;
    std::string component;
    std::vector<std::uint8_t> snapshot;

    CreateNodeEdit(WorldSystem& world, NodeAliases& aliases) : target(world, &aliases)
    {
        target.identity = std::make_shared<std::uint64_t>(0);
    }

    bool apply(std::string& error) override
    {
        auto* scene = target.world.scene();
        if (!scene || scene->id() != target.scene_id)
        {
            error = "The requested scene is no longer active";
            return false;
        }
        std::vector<Node> nodes;
        if (!scene->collect_nodes(nodes))
        {
            error = "Cannot collect scene entities";
            return false;
        }
        for (const auto& node : nodes)
        {
            if (node.name() == name)
            {
                error = "A node with this name already exists in the scene";
                return false;
            }
        }
        auto node = scene->create_node(name);
        if (!node)
        {
            error = "Cannot create scene node";
            return false;
        }
        if ((component == "light" && !node.add_component<Light>()) ||
            (component == "camera" && !node.add_component<Camera>()))
        {
            const bool removed = scene->destroy_node(node.id());
            error = removed ? "Cannot create requested component" : "Cannot clean up created node";
            return false;
        }
        target.node_id = node.id();
        *target.identity = node.id();
        (*target.aliases)[node.id()] = target.identity;
        snapshot = node_snapshot(node);
        return true;
    }

    bool revert(std::string& error) override
    {
        auto node = target.node(error);
        if (!node)
        {
            return false;
        }
        auto* scene = node.scene();
        if (node_snapshot(node) != snapshot ||
            (scene->active_camera() && scene->active_camera().node() == node))
        {
            error = "Created node changed outside this edit; undo is stale";
            return false;
        }
        std::vector<Node> nodes;
        if (!scene->collect_nodes(nodes))
        {
            error = "Cannot collect scene entities";
            return false;
        }
        for (const auto& other : nodes)
        {
            if (other.parent() == node)
            {
                error = "Created node has children; undo would remove unrelated nodes";
                return false;
            }
        }
        if (!scene->destroy_node(node.id()))
        {
            error = "Cannot remove created node";
            return false;
        }
        target.aliases->erase(target.node_id);
        return true;
    }
};

class ActiveCameraEdit final : public EditorEdit
{
public:
    Target target;
    std::uint64_t before = 0;
    std::shared_ptr<std::uint64_t> before_identity;
    explicit ActiveCameraEdit(Target value) : target(value)
    {
    }
    bool apply(std::string& error) override
    {
        return change(before_identity ? *before_identity : before, target.resolved_id(), error);
    }
    bool revert(std::string& error) override
    {
        return change(target.resolved_id(), before_identity ? *before_identity : before, error);
    }

private:
    bool change(std::uint64_t expected, std::uint64_t replacement, std::string& error)
    {
        auto* scene = target.world.scene();
        if (!scene || scene->id() != target.scene_id)
        {
            error = "The requested scene is no longer active";
            return false;
        }
        const auto current = scene->active_camera() ? scene->active_camera().node().id() : 0;
        auto node = replacement ? scene->find_node(replacement) : Node{};
        auto camera = node ? node.get_component<Camera>() : Camera{};
        if (current != expected || (replacement && !camera))
        {
            error = "Active camera changed outside this edit or target is missing";
            return false;
        }
        if (!scene->set_active_camera(camera))
        {
            error = "Cannot select active camera";
            return false;
        }
        return true;
    }
};

bool prepare_create_node(WorldSystem& world, NodeAliases& aliases, const Json& arguments,
                         Json& result, std::unique_ptr<EditorEdit>& edit, std::string& error)
{
    auto created = std::make_unique<CreateNodeEdit>(world, aliases);
    if (!fields(arguments, {"sceneId", "name", "component"}) ||
        !identifier(arguments, "sceneId", created->target.scene_id) ||
        !arguments.contains("name") || !arguments["name"].is_string() ||
        !arguments.contains("component") || !arguments["component"].is_string())
    {
        error = "create_node requires sceneId, name and component";
        return false;
    }
    created->name = arguments["name"].get<std::string>();
    created->component = arguments["component"].get<std::string>();
    if (created->name.empty() || created->name.size() > 128 ||
        created->name.find('\0') != std::string::npos ||
        (created->component != "empty" && created->component != "camera" &&
         created->component != "light"))
    {
        error = "Node name must contain 1..128 bytes and component must be empty, camera or light";
        return false;
    }
    result = {{"sceneId", arguments["sceneId"]}, {"name", created->name}};
    edit = std::move(created);
    return true;
}

bool prepare_active_camera(WorldSystem& world, NodeAliases& aliases, const Json& arguments,
                           Json& result, std::unique_ptr<EditorEdit>& edit, std::string& error)
{
    Target target(world, &aliases);
    if (!fields(arguments, {"sceneId", "nodeId"}) || !target.read(arguments, error))
    {
        error = "set_active_camera requires current sceneId and nodeId";
        return false;
    }
    auto node = target.node(error);
    if (!node || !node.get_component<Camera>())
    {
        error = "Requested node has no Camera component";
        return false;
    }
    auto record = std::make_unique<ActiveCameraEdit>(target);
    const auto previous = world.scene()->active_camera();
    record->before = previous ? previous.node().id() : 0;
    const auto previous_identity = aliases.find(record->before);
    if (previous_identity != aliases.end())
    {
        record->before_identity = previous_identity->second;
    }
    result = {{"sceneId", arguments["sceneId"]}, {"nodeId", arguments["nodeId"]}};
    edit = std::move(record);
    return true;
}

} // namespace

SceneCommands::SceneCommands(WorldSystem& world) noexcept : world_(world)
{
}

void SceneCommands::synchronize_aliases() noexcept
{
    const auto* scene = world_.scene();
    const auto generation = scene ? scene->id() : 0;
    if (scene_id_ != generation)
    {
        node_aliases_.clear();
        scene_id_ = generation;
    }
}

std::shared_ptr<std::uint64_t> SceneCommands::node_identity(std::uint64_t id)
{
    synchronize_aliases();
    if (!world_.scene() || !world_.scene()->find_node(id))
    {
        return {};
    }
    auto& identity = node_aliases_[id];
    if (!identity)
    {
        identity = std::make_shared<std::uint64_t>(id);
    }
    return identity;
}

bool SceneCommands::register_commands(EditorCommands& commands)
{
#if ENGINE_HAS_ASSET_PIPELINE
    if (!register_prefab_commands(world_, *this, node_aliases_, commands))
    {
        return false;
    }
#endif
    return commands.register_command("instantiate_model", true,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>& edit, std::string& error)
                                     {
                                         synchronize_aliases();
                                         return components::prepare_model(
                                             world_, node_aliases_, arguments, result, edit, error);
                                     }) &&
           components::register_collider(world_, commands, *this) &&
           components::register_rigid_body(world_, commands, *this) &&
           components::register_audio_source(world_, commands, *this) &&
           components::register_audio_listener(world_, commands, *this) &&
           components::register_animator(world_, commands, *this) &&
           components::register_character_motor(world_, commands, *this) &&
           commands.register_command("get_scene", false,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>&, std::string& error)
                                     { return query_scene(world_, arguments, result, error); }) &&
           commands.register_command("create_node", true,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>& edit, std::string& error)
                                     {
                                         synchronize_aliases();
                                         return prepare_create_node(world_, node_aliases_,
                                                                    arguments, result, edit, error);
                                     }) &&
           commands.register_command("set_node_state", true,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>& edit, std::string& error)
                                     {
                                         synchronize_aliases();
                                         return prepare_state(world_, node_aliases_, arguments,
                                                              result, edit, error, false);
                                     }) &&
           commands.register_command("set_component_state", true,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>& edit, std::string& error)
                                     {
                                         synchronize_aliases();
                                         return prepare_state(world_, node_aliases_, arguments,
                                                              result, edit, error, true);
                                     }) &&
           commands.register_command("set_active_camera", true,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>& edit, std::string& error)
                                     {
                                         synchronize_aliases();
                                         return prepare_active_camera(
                                             world_, node_aliases_, arguments, result, edit, error);
                                     }) &&
           commands.register_command("set_transform", true,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>& edit, std::string& error)
                                     {
                                         synchronize_aliases();
                                         return components::prepare_transform(
                                             world_, node_aliases_, arguments, result, edit, error);
                                     }) &&
           commands.register_command("set_light", true,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>& edit, std::string& error)
                                     {
                                         synchronize_aliases();
                                         return components::prepare_light(
                                             world_, node_aliases_, arguments, result, edit, error);
                                     }) &&
           commands.register_command("set_material_blend_mode", true,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>& edit, std::string& error)
                                     {
                                         synchronize_aliases();
                                         return components::prepare_material_blend(
                                             world_, node_aliases_, arguments, result, edit, error);
                                     }) &&
           commands.register_command("set_material_type", true,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>& edit, std::string& error)
                                     {
                                         synchronize_aliases();
                                         return components::prepare_material_type(
                                             world_, node_aliases_, arguments, result, edit, error);
                                     }) &&
           commands.register_command("set_material_parameter", true,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>& edit, std::string& error)
                                     {
                                         synchronize_aliases();
                                         return components::prepare_material(
                                             world_, node_aliases_, arguments, result, edit, error);
                                     }) &&
           commands.register_command("set_camera", true,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>& edit, std::string& error)
                                     {
                                         synchronize_aliases();
                                         return components::prepare_camera(
                                             world_, node_aliases_, arguments, result, edit, error);
                                     }) &&
           commands.register_command("set_material_texture", true,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>& edit, std::string& error)
                                     {
                                         synchronize_aliases();
                                         return components::prepare_texture(
                                             world_, node_aliases_, arguments, result, edit, error);
                                     });
}

} // namespace engine::editor
