#include "editor/commands/scene_commands.hpp"

#include "editor/commands/components/component_edit.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/editor_edit.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::editor
{
namespace
{
using Json = nlohmann::json;
using NodeAliases = std::map<std::uint64_t, std::shared_ptr<std::uint64_t>>;

bool fields(const Json& arguments, std::initializer_list<std::string_view> allowed)
{
    if (!arguments.is_object())
    {
        return false;
    }
    for (const auto& item : arguments.items())
    {
        if (std::find(allowed.begin(), allowed.end(), item.key()) == allowed.end())
        {
            return false;
        }
    }
    return true;
}

bool identifier(const Json& arguments, const char* name, std::uint64_t& value)
{
    if (!arguments.contains(name) || !arguments[name].is_string())
    {
        return false;
    }
    const auto& text = arguments[name].get_ref<const std::string&>();
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value != 0 &&
           text == std::to_string(value);
}

bool scalar(const Json& input, double& output)
{
    if (!input.is_number())
    {
        return false;
    }
    output = input.get<double>();
    return std::isfinite(output);
}

bool vector(const Json& input, std::size_t count, std::array<double, 4>& values)
{
    if (!input.is_array() || input.size() != count)
    {
        return false;
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        if (!scalar(input[index], values[index]))
        {
            return false;
        }
    }
    return true;
}

bool same(const Vec3& first, const Vec3& second)
{
    return first.x() == second.x() && first.y() == second.y() && first.z() == second.z();
}

bool same(const Color& first, const Color& second)
{
    return first.r() == second.r() && first.g() == second.g() && first.b() == second.b() &&
           first.a() == second.a();
}

Json json_vector(const Vec3& value)
{
    return Json::array({value.x(), value.y(), value.z()});
}

struct Target
{
    WorldSystem& world;
    NodeAliases* aliases = nullptr;
    std::shared_ptr<std::uint64_t> identity;
    std::uint64_t scene_id = 0;
    std::uint64_t node_id = 0;

    explicit Target(WorldSystem& value, NodeAliases* references = nullptr)
        : world(value), aliases(references)
    {
    }

    std::uint64_t resolved_id() const noexcept
    {
        return identity ? *identity : node_id;
    }

    Node* node(std::string& error) const
    {
        auto* scene = world.scene();
        if (scene == nullptr || scene->id() != scene_id)
        {
            error = "The requested scene is no longer active";
            return nullptr;
        }
        auto* found = scene->find_node(resolved_id());
        if (found == nullptr)
        {
            error = "The requested node no longer exists";
        }
        return found;
    }

    bool read(const Json& arguments, std::string& error)
    {
        if (!identifier(arguments, "sceneId", scene_id) ||
            !identifier(arguments, "nodeId", node_id))
        {
            error = "sceneId and nodeId must be complete nonzero uint64 decimal strings";
            return false;
        }
        if (!node(error))
        {
            return false;
        }
        if (aliases)
        {
            const auto found = aliases->find(node_id);
            if (found != aliases->end())
            {
                identity = found->second;
            }
        }
        return true;
    }
};

std::vector<std::uint8_t> node_snapshot(const Node& node)
{
    schema::NodeDataT data;
    data.info = std::make_unique<schema::NodeInfoT>(node.info());
    for (const auto& component : node.components())
    {
        data.components.push_back(std::make_unique<schema::ComponentDataT>(component->data()));
    }
    flatbuffers::FlatBufferBuilder builder;
    builder.Finish(schema::NodeData::Pack(builder, &data));
    return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
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
        for (const auto& node : scene->nodes())
        {
            if (node->name() == name)
            {
                error = "A node with this name already exists in the scene";
                return false;
            }
        }
        auto* node = scene->create_node(name);
        if (!node)
        {
            error = "Cannot create scene node";
            return false;
        }
        if ((component == "light" && !node->add_component<Light>()) ||
            (component == "camera" && !node->add_component<Camera>()))
        {
            const bool removed = scene->destroy_node(node->id());
            error = removed ? "Cannot create requested component" : "Cannot clean up created node";
            return false;
        }
        target.node_id = node->id();
        *target.identity = node->id();
        (*target.aliases)[node->id()] = target.identity;
        snapshot = node_snapshot(*node);
        return true;
    }

    bool revert(std::string& error) override
    {
        auto* node = target.node(error);
        if (!node)
        {
            return false;
        }
        auto& scene = node->scene();
        if (node_snapshot(*node) != snapshot ||
            (scene.active_camera() && &scene.active_camera()->node() == node))
        {
            error = "Created node changed outside this edit; undo is stale";
            return false;
        }
        for (const auto& other : scene.nodes())
        {
            if (other->parent() == node)
            {
                error = "Created node has children; undo would remove unrelated nodes";
                return false;
            }
        }
        if (!scene.destroy_node(node->id()))
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
        const auto current = scene->active_camera() ? scene->active_camera()->node().id() : 0;
        auto* node = replacement ? scene->find_node(replacement) : nullptr;
        auto* camera = node ? node->get_component<Camera>() : nullptr;
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
    auto* node = target.node(error);
    if (!node || !node->get_component<Camera>())
    {
        error = "Requested node has no Camera component";
        return false;
    }
    auto record = std::make_unique<ActiveCameraEdit>(target);
    const auto* previous = world.scene()->active_camera();
    record->before = previous ? previous->node().id() : 0;
    const auto previous_identity = aliases.find(record->before);
    if (previous_identity != aliases.end())
    {
        record->before_identity = previous_identity->second;
    }
    result = {{"sceneId", arguments["sceneId"]}, {"nodeId", arguments["nodeId"]}};
    edit = std::move(record);
    return true;
}

class TransformEdit final : public EditorEdit
{
public:
    Target target;
    TransformSettings before;
    TransformSettings after;
    bool position = false;
    bool rotation = false;
    bool scale = false;

    explicit TransformEdit(Target value) : target(value)
    {
    }

    bool apply(std::string& error) override
    {
        return change(before, after, error);
    }

    bool revert(std::string& error) override
    {
        return change(after, before, error);
    }

private:
    bool change(const TransformSettings& expected, const TransformSettings& replacement,
                std::string& error)
    {
        auto* node = target.node(error);
        auto* component = node != nullptr ? node->get_component<Transform>() : nullptr;
        if (component == nullptr)
        {
            error = "Transform target no longer exists";
            return false;
        }
        auto current = component->settings();
        if ((position && !same(current.position, expected.position)) ||
            (rotation && !same(current.rotation, expected.rotation)) ||
            (scale && !same(current.scale, expected.scale)))
        {
            error = "Transform changed outside this edit; undo or redo is stale";
            return false;
        }
        if (position)
        {
            current.position = replacement.position;
        }
        if (rotation)
        {
            current.rotation = replacement.rotation;
        }
        if (scale)
        {
            current.scale = replacement.scale;
        }
        if (!component->set_settings(current))
        {
            error = "Invalid transform values";
            return false;
        }
        return true;
    }
};

class LightEdit final : public EditorEdit
{
public:
    Target target;
    LightSettings before;
    LightSettings after;
    bool color = false;
    bool intensity = false;
    bool kind = false;
    bool range = false;
    bool inner_angle = false;
    bool outer_angle = false;

    explicit LightEdit(Target value) : target(value)
    {
    }

    bool apply(std::string& error) override
    {
        return change(before, after, error);
    }

    bool revert(std::string& error) override
    {
        return change(after, before, error);
    }

private:
    bool change(const LightSettings& expected, const LightSettings& replacement, std::string& error)
    {
        auto* node = target.node(error);
        auto* component = node != nullptr ? node->get_component<Light>() : nullptr;
        if (component == nullptr)
        {
            error = "Light target no longer exists";
            return false;
        }
        auto current = component->settings();
        if ((color && !same(current.color, expected.color)) ||
            (intensity && current.intensity != expected.intensity) ||
            (kind && current.kind != expected.kind) || (range && current.range != expected.range) ||
            (inner_angle && current.inner_angle != expected.inner_angle) ||
            (outer_angle && current.outer_angle != expected.outer_angle))
        {
            error = "Light changed outside this edit; undo or redo is stale";
            return false;
        }
        if (color)
        {
            current.color = replacement.color;
        }
        if (intensity)
        {
            current.intensity = replacement.intensity;
        }
        if (kind)
        {
            current.kind = replacement.kind;
        }
        if (range)
        {
            current.range = replacement.range;
        }
        if (inner_angle)
        {
            current.inner_angle = replacement.inner_angle;
        }
        if (outer_angle)
        {
            current.outer_angle = replacement.outer_angle;
        }
        if (!component->set_settings(current))
        {
            error = "Invalid light values";
            return false;
        }
        return true;
    }
};

bool read_parameter(const Material& material, std::string_view name, std::string_view type,
                    std::array<double, 4>& output)
{
    output = {};
    if (type == "float")
    {
        float value = 0;
        if (!material.get_float(name, value))
        {
            return false;
        }
        output[0] = value;
        return true;
    }
    if (type == "v2")
    {
        Vec2 value;
        if (!material.get_v2(name, value))
        {
            return false;
        }
        output = {value.x(), value.y(), 0, 0};
        return true;
    }
    if (type == "v3")
    {
        Vec3 value;
        if (!material.get_v3(name, value))
        {
            return false;
        }
        output = {value.x(), value.y(), value.z(), 0};
        return true;
    }
    if (type == "v4")
    {
        Vec4 value;
        if (!material.get_v4(name, value))
        {
            return false;
        }
        output = {value.x(), value.y(), value.z(), value.w()};
        return true;
    }
    return false;
}

class MaterialEdit final : public EditorEdit
{
public:
    Target target;
    std::size_t slot = 0;
    std::string name;
    std::string type;
    std::string material_path;
    std::string shader_path;
    std::array<double, 4> before{};
    std::array<double, 4> after{};

    explicit MaterialEdit(Target value) : target(value)
    {
    }

    bool apply(std::string& error) override
    {
        return change(before, after, error);
    }

    bool revert(std::string& error) override
    {
        return change(after, before, error);
    }

private:
    bool change(const std::array<double, 4>& expected, const std::array<double, 4>& replacement,
                std::string& error)
    {
        auto* node = target.node(error);
        auto* component = node != nullptr ? node->get_component<MeshRenderer>() : nullptr;
        if (component == nullptr)
        {
            error = "Material target no longer exists";
            return false;
        }
        auto* selected = component->material(slot);
        if (selected == nullptr)
        {
            error = "Material slot no longer exists";
            return false;
        }
        auto& material = *selected;
        std::array<double, 4> current{};
        if (material.type_path() != material_path || material.shader_path() != shader_path ||
            !read_parameter(material, name, type, current) || current != expected)
        {
            error = "Material changed outside this edit; undo or redo is stale";
            return false;
        }
        bool success = false;
        if (type == "float")
        {
            success = material.set_float(name, static_cast<float>(replacement[0]));
        }
        else if (type == "v2")
        {
            success = material.set_v2(name, {replacement[0], replacement[1]});
        }
        else if (type == "v3")
        {
            success = material.set_v3(name, {replacement[0], replacement[1], replacement[2]});
        }
        else if (type == "v4")
        {
            success = material.set_v4(
                name, {replacement[0], replacement[1], replacement[2], replacement[3]});
        }
        if (!success)
        {
            error = "Invalid material parameter type or value";
        }
        return success;
    }
};

bool prepare_transform(WorldSystem& world, NodeAliases& aliases, const Json& arguments,
                       Json& result, std::unique_ptr<EditorEdit>& output, std::string& error)
{
    Target target(world, &aliases);
    if (!fields(arguments, {"sceneId", "nodeId", "position", "rotation", "scale"}) ||
        arguments.size() <= 2 || !target.read(arguments, error))
    {
        if (error.empty())
        {
            error = "set_transform requires sceneId, nodeId and transform fields";
        }
        return false;
    }
    const auto* component = target.node(error)->get_component<Transform>();
    if (component == nullptr)
    {
        error = "Node has no Transform";
        return false;
    }
    auto edit = std::make_unique<TransformEdit>(target);
    edit->before = component->settings();
    edit->after = edit->before;
    for (const char* name : {"position", "rotation", "scale"})
    {
        if (!arguments.contains(name))
        {
            continue;
        }
        std::array<double, 4> values{};
        if (!vector(arguments[name], 3, values))
        {
            error = "Transform fields require three finite numbers";
            return false;
        }
        const Vec3 value{values[0], values[1], values[2]};
        if (std::string_view(name) == "position")
        {
            edit->position = true;
            edit->after.position = value;
        }
        else if (std::string_view(name) == "rotation")
        {
            edit->rotation = true;
            edit->after.rotation = value;
        }
        else
        {
            edit->scale = true;
            edit->after.scale = value;
        }
    }
    result = {{"sceneId", std::to_string(target.scene_id)}};
    output = std::move(edit);
    return true;
}

bool prepare_light(WorldSystem& world, NodeAliases& aliases, const Json& arguments, Json& result,
                   std::unique_ptr<EditorEdit>& output, std::string& error)
{
    Target target(world, &aliases);
    if (!fields(arguments, {"sceneId", "nodeId", "color", "intensity", "kind", "range",
                            "innerAngle", "outerAngle"}) ||
        arguments.size() <= 2 || !target.read(arguments, error))
    {
        if (error.empty())
        {
            error = "set_light requires sceneId, nodeId and light fields";
        }
        return false;
    }
    const auto* component = target.node(error)->get_component<Light>();
    if (component == nullptr)
    {
        error = "Node has no Light";
        return false;
    }
    auto edit = std::make_unique<LightEdit>(target);
    edit->before = component->settings();
    edit->after = edit->before;
    if (arguments.contains("color"))
    {
        std::array<double, 4> values{};
        if (!vector(arguments["color"], 4, values) ||
            !std::all_of(values.begin(), values.end(),
                         [](double value) { return value >= 0 && value <= 1; }))
        {
            error = "Light color requires four numbers in [0, 1]";
            return false;
        }
        edit->color = true;
        edit->after.color = Color(static_cast<float>(values[0]), static_cast<float>(values[1]),
                                  static_cast<float>(values[2]), static_cast<float>(values[3]));
    }
    if (arguments.contains("intensity"))
    {
        double value = 0;
        if (!scalar(arguments["intensity"], value) || value < 0 ||
            value > std::numeric_limits<float>::max())
        {
            error = "Light intensity must be a finite nonnegative float";
            return false;
        }
        edit->intensity = true;
        edit->after.intensity = static_cast<float>(value);
    }
    if (arguments.contains("kind"))
    {
        if (!arguments["kind"].is_string())
        {
            error = "Light kind must be directional, point or spot";
            return false;
        }
        const auto value = arguments["kind"].get<std::string>();
        if (value == "directional")
        {
            edit->after.kind = schema::LightKind::directional;
        }
        else if (value == "point")
        {
            edit->after.kind = schema::LightKind::point;
        }
        else if (value == "spot")
        {
            edit->after.kind = schema::LightKind::spot;
        }
        else
        {
            error = "Unknown light kind";
            return false;
        }
        edit->kind = true;
    }
    for (const char* name : {"range", "innerAngle", "outerAngle"})
    {
        if (!arguments.contains(name))
        {
            continue;
        }
        double value = 0;
        if (!scalar(arguments[name], value) || std::abs(value) > std::numeric_limits<float>::max())
        {
            error = "Light fields require finite floats";
            return false;
        }
        if (std::string_view(name) == "range")
        {
            edit->range = true;
            edit->after.range = static_cast<float>(value);
        }
        else if (std::string_view(name) == "innerAngle")
        {
            edit->inner_angle = true;
            edit->after.inner_angle = static_cast<float>(value);
        }
        else
        {
            edit->outer_angle = true;
            edit->after.outer_angle = static_cast<float>(value);
        }
    }
    result = {{"sceneId", std::to_string(target.scene_id)}};
    output = std::move(edit);
    return true;
}

bool read_slot(const Json& arguments, std::size_t& output)
{
    output = 0;
    if (!arguments.contains("slot"))
    {
        return true;
    }
    const auto& value = arguments["slot"];
    if (!value.is_number_integer() || value < 0 || value >= 16)
    {
        return false;
    }
    output = value.get<std::size_t>();
    return true;
}

bool prepare_material(WorldSystem& world, NodeAliases& aliases, const Json& arguments, Json& result,
                      std::unique_ptr<EditorEdit>& output, std::string& error)
{
    Target target(world, &aliases);
    if (!fields(arguments, {"sceneId", "nodeId", "name", "type", "value", "slot"}) ||
        !arguments.contains("name") || !arguments.contains("type") ||
        !arguments.contains("value") || !arguments["name"].is_string() ||
        !arguments["type"].is_string() || !target.read(arguments, error))
    {
        if (error.empty())
        {
            error = "set_material_parameter requires sceneId, nodeId, name, type and value";
        }
        return false;
    }
    const auto* component = target.node(error)->get_component<MeshRenderer>();
    if (component == nullptr)
    {
        error = "Node has no MeshRenderer";
        return false;
    }
    auto edit = std::make_unique<MaterialEdit>(target);
    edit->name = arguments["name"].get<std::string>();
    edit->type = arguments["type"].get<std::string>();
    if (!read_slot(arguments, edit->slot) || !component->material(edit->slot))
    {
        error = "Invalid material slot";
        return false;
    }
    const auto& material = *component->material(edit->slot);
    edit->material_path = material.type_path();
    edit->shader_path = material.shader_path();
    if (!read_parameter(material, edit->name, edit->type, edit->before))
    {
        error = "Unknown material parameter or mismatched type";
        return false;
    }
    const std::size_t count = edit->type == "float" ? 1
                              : edit->type == "v2"  ? 2
                              : edit->type == "v3"  ? 3
                                                    : 4;
    if (!(count == 1 ? scalar(arguments["value"], edit->after[0])
                     : vector(arguments["value"], count, edit->after)) ||
        !std::all_of(edit->after.begin(), edit->after.end(), [](double value)
                     { return std::abs(value) <= std::numeric_limits<float>::max(); }))
    {
        error = "Material value must match its finite numeric type";
        return false;
    }
    if (count == 1)
    {
        edit->after[0] = static_cast<float>(edit->after[0]);
    }
    result = {{"sceneId", std::to_string(target.scene_id)}};
    output = std::move(edit);
    return true;
}

class CameraEdit final : public EditorEdit
{
public:
    explicit CameraEdit(Target value) : target(value)
    {
    }
    Target target;
    CameraSettings before;
    CameraSettings after;
    bool projection = false;
    bool fov = false;
    bool near_clip = false;
    bool far_clip = false;
    bool size = false;
    bool apply(std::string& error) override
    {
        return change(before, after, error);
    }
    bool revert(std::string& error) override
    {
        return change(after, before, error);
    }

private:
    bool change(const CameraSettings& expected, const CameraSettings& replacement,
                std::string& error)
    {
        auto* node = target.node(error);
        auto* camera = node ? node->get_component<Camera>() : nullptr;
        if (!camera)
        {
            error = "Camera target no longer exists";
            return false;
        }
        auto current = camera->settings();
        if ((projection && current.projection != expected.projection) ||
            (fov && current.fov_degrees != expected.fov_degrees) ||
            (near_clip && current.near_clip != expected.near_clip) ||
            (far_clip && current.far_clip != expected.far_clip) ||
            (size && current.orthographic_size != expected.orthographic_size))
        {
            error = "Camera changed outside this edit; undo or redo is stale";
            return false;
        }
        if (projection)
        {
            current.projection = replacement.projection;
        }
        if (fov)
        {
            current.fov_degrees = replacement.fov_degrees;
        }
        if (near_clip)
        {
            current.near_clip = replacement.near_clip;
        }
        if (far_clip)
        {
            current.far_clip = replacement.far_clip;
        }
        if (size)
        {
            current.orthographic_size = replacement.orthographic_size;
        }
        if (!camera->set_settings(current))
        {
            error = "Invalid camera settings";
            return false;
        }
        return true;
    }
};

bool prepare_camera(WorldSystem& world, NodeAliases& aliases, const Json& arguments, Json& result,
                    std::unique_ptr<EditorEdit>& output, std::string& error)
{
    Target target(world, &aliases);
    if (!fields(arguments, {"sceneId", "nodeId", "projection", "fovDegrees", "nearClip", "farClip",
                            "orthographicSize"}) ||
        arguments.size() <= 2 || !target.read(arguments, error))
    {
        if (error.empty())
        {
            error = "set_camera requires camera fields";
        }
        return false;
    }
    const auto* camera = target.node(error)->get_component<Camera>();
    if (!camera)
    {
        error = "Node has no Camera";
        return false;
    }
    auto edit = std::make_unique<CameraEdit>(target);
    edit->before = camera->settings();
    edit->after = edit->before;
    if (arguments.contains("projection"))
    {
        if (arguments["projection"] == "perspective")
        {
            edit->after.projection = schema::Projection::perspective;
        }
        else if (arguments["projection"] == "orthographic")
        {
            edit->after.projection = schema::Projection::orthographic;
        }
        else
        {
            error = "Unknown camera projection";
            return false;
        }
        edit->projection = true;
    }
    for (const char* name : {"fovDegrees", "nearClip", "farClip", "orthographicSize"})
    {
        if (!arguments.contains(name))
        {
            continue;
        }
        double value = 0;
        if (!scalar(arguments[name], value) || std::abs(value) > std::numeric_limits<float>::max())
        {
            error = "Camera fields require finite floats";
            return false;
        }
        const auto number = static_cast<float>(value);
        if (std::string_view(name) == "fovDegrees")
        {
            edit->fov = true;
            edit->after.fov_degrees = number;
        }
        else if (std::string_view(name) == "nearClip")
        {
            edit->near_clip = true;
            edit->after.near_clip = number;
        }
        else if (std::string_view(name) == "farClip")
        {
            edit->far_clip = true;
            edit->after.far_clip = number;
        }
        else
        {
            edit->size = true;
            edit->after.orthographic_size = number;
        }
    }
    result = {{"sceneId", std::to_string(target.scene_id)}};
    output = std::move(edit);
    return true;
}

class TextureEdit final : public EditorEdit
{
public:
    explicit TextureEdit(Target value) : target(value)
    {
    }
    Target target;
    std::size_t slot = 0;
    std::string name;
    std::string material_path;
    std::string shader_path;
    schema::AssetEntryT before;
    schema::AssetEntryT after;
    bool apply(std::string& error) override
    {
        return change(before, after, error);
    }
    bool revert(std::string& error) override
    {
        return change(after, before, error);
    }

private:
    bool change(const schema::AssetEntryT& expected, const schema::AssetEntryT& replacement,
                std::string& error)
    {
        auto* node = target.node(error);
        auto* renderer = node ? node->get_component<MeshRenderer>() : nullptr;
        auto* material = renderer ? renderer->material(slot) : nullptr;
        auto* resources = ResourceSystem::instance();
        const auto* available = resources ? resources->catalog().find(replacement.guid) : nullptr;
        const auto current = material ? material->texture(name) : ResourcePtr{};
        if (!current || current->info() != expected || !available || *available != replacement ||
            !material || material->type_path() != material_path ||
            material->shader_path() != shader_path || !current->texture())
        {
            error = "Texture target changed; undo or redo is stale";
            return false;
        }
        if (!material->set_texture(name, replacement.path))
        {
            error = "Texture assignment failed";
            return false;
        }
        return true;
    }
};

bool prepare_texture(WorldSystem& world, NodeAliases& aliases, const Json& arguments, Json& result,
                     std::unique_ptr<EditorEdit>& output, std::string& error)
{
    Target target(world, &aliases);
    if (!fields(arguments, {"sceneId", "nodeId", "name", "path", "slot"}) ||
        !arguments.contains("name") || !arguments["name"].is_string() ||
        !arguments.contains("path") || !arguments["path"].is_string() ||
        !target.read(arguments, error))
    {
        if (error.empty())
        {
            error = "set_material_texture requires name and resource path";
        }
        return false;
    }
    const auto* renderer = target.node(error)->get_component<MeshRenderer>();
    auto edit = std::make_unique<TextureEdit>(target);
    if (!renderer || !read_slot(arguments, edit->slot) || !renderer->material(edit->slot))
    {
        error = "Invalid material slot";
        return false;
    }
    const auto& material = *renderer->material(edit->slot);
    edit->name = arguments["name"].get<std::string>();
    std::string_view previous_path;
    Guid guid;
    ResourcePtr replacement;
    auto* resources = ResourceSystem::instance();
    if (!material.get_texture(edit->name, previous_path) || !resources ||
        !ResourceSystem::path_to_guid(arguments["path"].get<std::string>(), guid) ||
        !resources->loader().load(guid, replacement) || !replacement->texture())
    {
        error = "Unknown texture slot or texture resource";
        return false;
    }
    edit->before = material.texture(edit->name)->info();
    edit->after = replacement->info();
    edit->material_path = material.type_path();
    edit->shader_path = material.shader_path();
    result = {{"sceneId", std::to_string(target.scene_id)}};
    output = std::move(edit);
    return true;
}

Json material_json(const Material& material)
{
    Json parameters = Json::array();
    const auto* shader = material.shader_data();
    if (shader != nullptr)
    {
        for (const auto& parameter : shader->parameters)
        {
            if (!parameter->exposed)
            {
                continue;
            }
            std::string type;
            std::size_t count = 0;
            switch (parameter->default_value.type)
            {
            case schema::MaterialValue::FloatValue:
                type = "float";
                count = 1;
                break;
            case schema::MaterialValue::Vec2Value:
                type = "v2";
                count = 2;
                break;
            case schema::MaterialValue::Vec3Value:
                type = "v3";
                count = 3;
                break;
            case schema::MaterialValue::Vec4Value:
                type = "v4";
                count = 4;
                break;
            default:
                continue;
            }
            std::array<double, 4> values{};
            if (!read_parameter(material, parameter->name, type, values))
            {
                continue;
            }
            Json value = values[0];
            if (count != 1)
            {
                value = Json::array();
                for (std::size_t index = 0; index < count; ++index)
                {
                    value.push_back(values[index]);
                }
            }
            parameters.push_back({{"name", parameter->name}, {"type", type}, {"value", value}});
        }
    }
    Json textures = Json::array();
    if (shader)
    {
        for (const auto& sampler : shader->samplers)
        {
            std::string_view path;
            if (material.get_texture(sampler->name, path))
            {
                textures.push_back({{"name", sampler->name}, {"path", path}});
            }
        }
    }
    return {{"textures", std::move(textures)},
            {"typePath", material.type_path()},
            {"shaderPath", material.shader_path()},
            {"parameters", std::move(parameters)}};
}

bool restore_material(Material& material, const Json& saved)
{
    if (!material.set_type(saved["typePath"].get<std::string>()) ||
        !material.set_shader(saved["shaderPath"].get<std::string>()))
    {
        return false;
    }
    for (const auto& parameter : saved["parameters"])
    {
        const auto name = parameter["name"].get<std::string>();
        const auto type = parameter["type"].get<std::string>();
        const auto& value = parameter["value"];
        bool success = false;
        if (type == "float")
        {
            success = material.set_float(name, value.get<float>());
        }
        else if (type == "v2")
        {
            success = material.set_v2(name, {value[0].get<double>(), value[1].get<double>()});
        }
        else if (type == "v3")
        {
            success = material.set_v3(
                name, {value[0].get<double>(), value[1].get<double>(), value[2].get<double>()});
        }
        else if (type == "v4")
        {
            success = material.set_v4(name, {value[0].get<double>(), value[1].get<double>(),
                                             value[2].get<double>(), value[3].get<double>()});
        }
        if (!success)
        {
            return false;
        }
    }
    for (const auto& texture : saved["textures"])
    {
        if (!material.set_texture(texture["name"].get<std::string>(),
                                  texture["path"].get<std::string>()))
        {
            return false;
        }
    }
    return true;
}

class MaterialTypeEdit final : public EditorEdit
{
public:
    Target target;
    std::size_t slot = 0;
    std::string path;
    Json before;
    Json after;
    std::vector<schema::AssetEntryT> versions;

    explicit MaterialTypeEdit(Target value) : target(value)
    {
    }

    bool apply(std::string& error) override
    {
        return change(before, after, error);
    }
    bool revert(std::string& error) override
    {
        return change(after, before, error);
    }
    bool retain_version(std::string_view resource_path)
    {
        auto* resources = ResourceSystem::instance();
        Guid guid;
        ResourcePtr resource;
        if (!resources || !ResourceSystem::path_to_guid(resource_path, guid) ||
            !resources->loader().load(guid, resource))
        {
            return false;
        }
        if (std::any_of(versions.begin(), versions.end(),
                        [&](const auto& entry) { return entry.guid == guid; }))
        {
            return true;
        }
        versions.push_back(resource->info());
        for (const auto& dependency : resource->info().dependencies)
        {
            std::string dependency_path;
            if (!ResourceSystem::guid_to_path(dependency, dependency_path) ||
                !retain_version(dependency_path))
            {
                return false;
            }
        }
        return true;
    }

private:
    bool change(const Json& expected, Json& replacement, std::string& error)
    {
        auto* node = target.node(error);
        auto* renderer = node ? node->get_component<MeshRenderer>() : nullptr;
        auto* material = renderer ? renderer->material(slot) : nullptr;
        auto* resources = ResourceSystem::instance();
        if (!material || !resources || material_json(*material) != expected)
        {
            error = "Material changed outside this edit; undo or redo is stale";
            return false;
        }
        std::vector<ResourcePtr> pins;
        for (const auto& version : versions)
        {
            const auto* available = resources->catalog().find(version.guid);
            ResourcePtr resource;
            if (!available || *available != version ||
                !resources->loader().load(version.guid, resource))
            {
                error = "Material dependency version changed or is unavailable";
                return false;
            }
            pins.push_back(std::move(resource));
        }
        if (replacement.is_null())
        {
            if (!material->set_type(path))
            {
                error = "Cannot assign material type";
                return false;
            }
            replacement = material_json(*material);
            return true;
        }
        if (!restore_material(*material, replacement))
        {
            const bool restored = restore_material(*material, expected);
            error =
                restored ? "Cannot restore material values" : "Cannot roll back material values";
            return false;
        }
        return true;
    }
};

bool prepare_material_type(WorldSystem& world, NodeAliases& aliases, const Json& arguments,
                           Json& result, std::unique_ptr<EditorEdit>& output, std::string& error)
{
    Target target(world, &aliases);
    if (!fields(arguments, {"sceneId", "nodeId", "slot", "path"}) || !arguments.contains("path") ||
        !arguments["path"].is_string() || !target.read(arguments, error))
    {
        error = "set_material_type requires current sceneId, nodeId and resource path";
        return false;
    }
    auto record = std::make_unique<MaterialTypeEdit>(target);
    const auto* renderer = target.node(error)->get_component<MeshRenderer>();
    if (!renderer || !read_slot(arguments, record->slot) || !renderer->material(record->slot))
    {
        error = "Invalid material slot";
        return false;
    }
    const auto& material = *renderer->material(record->slot);
    record->path = arguments["path"].get<std::string>();
    Guid guid;
    ResourcePtr requested;
    auto* resources = ResourceSystem::instance();
    if (!resources || !ResourceSystem::path_to_guid(record->path, guid) ||
        !resources->loader().load(guid, requested) || !requested->material())
    {
        error = "Requested path does not identify a material";
        return false;
    }
    record->before = material_json(material);
    if (!record->retain_version(material.type_path()) ||
        !record->retain_version(material.shader_path()) || !record->retain_version(record->path))
    {
        error = "Cannot capture material resource versions";
        return false;
    }
    for (const auto& texture : record->before["textures"])
    {
        if (!record->retain_version(texture["path"].get<std::string>()))
        {
            error = "Cannot capture material texture version";
            return false;
        }
    }
    result = {{"sceneId", arguments["sceneId"]},
              {"nodeId", arguments["nodeId"]},
              {"path", record->path},
              {"slot", record->slot}};
    output = std::move(record);
    return true;
}

bool query_scene(WorldSystem& world, const Json& arguments, Json& result, std::string& error)
{
    if (!arguments.empty())
    {
        error = "get_scene takes no arguments";
        return false;
    }
    const auto* scene = world.scene();
    result = {{"scene", nullptr}};
    if (scene == nullptr)
    {
        return true;
    }
    Json nodes = Json::array();
    for (const auto& node : scene->nodes())
    {
        Json components = Json::array();
        for (const auto& component : node->components())
        {
            Json data = {{"enabled", component->enabled()}};
            switch (component->type())
            {
            case ComponentType::TransformData:
            {
                const auto& value = node->get_component<Transform>()->settings();
                data.update({{"type", "transform"},
                             {"position", json_vector(value.position)},
                             {"rotation", json_vector(value.rotation)},
                             {"scale", json_vector(value.scale)}});
                break;
            }
            case ComponentType::CameraData:
            {
                const auto& value = node->get_component<Camera>()->settings();
                data.update({{"type", "camera"},
                             {"fovDegrees", value.fov_degrees},
                             {"nearClip", value.near_clip},
                             {"farClip", value.far_clip},
                             {"projection", schema::EnumNameProjection(value.projection)},
                             {"orthographicSize", value.orthographic_size}});
                break;
            }
            case ComponentType::LightData:
            {
                const auto& value = node->get_component<Light>()->settings();
                data.update({{"type", "light"},
                             {"color",
                              {value.color.r(), value.color.g(), value.color.b(), value.color.a()}},
                             {"intensity", value.intensity},
                             {"kind", schema::EnumNameLightKind(value.kind)},
                             {"range", value.range},
                             {"innerAngle", value.inner_angle},
                             {"outerAngle", value.outer_angle}});
                break;
            }
            case ComponentType::MeshRendererData:
            {
                const auto& value = *node->get_component<MeshRenderer>();
                Json materials = Json::array();
                for (std::size_t slot = 0; slot < value.material_count(); ++slot)
                {
                    materials.push_back(material_json(*value.material(slot)));
                }
                data["materials"] = std::move(materials);
                data.update({{"type", "mesh_renderer"},
                             {"meshPath", value.mesh_path()},
                             {"material", material_json(value.material())}});
                break;
            }
            default:
                data["type"] = schema::EnumNameComponentPayload(component->type());
                break;
            }
            components.push_back(std::move(data));
        }
        nodes.push_back(
            {{"nodeId", std::to_string(node->id())},
             {"name", node->name()},
             {"parentId", node->parent() == nullptr ? Json(nullptr)
                                                    : Json(std::to_string(node->parent()->id()))},
             {"components", std::move(components)}});
    }
    const auto* camera = scene->active_camera();
    result["scene"] = {{"sceneId", std::to_string(scene->id())},
                       {"name", scene->name()},
                       {"activeCameraNodeId", camera == nullptr
                                                  ? Json(nullptr)
                                                  : Json(std::to_string(camera->node().id()))},
                       {"nodes", std::move(nodes)}};
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

bool SceneCommands::register_commands(EditorCommands& commands)
{
    return components::register_collider(world_, commands, node_aliases_) &&
           components::register_rigid_body(world_, commands, node_aliases_) &&
           components::register_audio_source(world_, commands, node_aliases_) &&
           components::register_audio_listener(world_, commands, node_aliases_) &&
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
                                         return prepare_transform(world_, node_aliases_, arguments,
                                                                  result, edit, error);
                                     }) &&
           commands.register_command("set_light", true,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>& edit, std::string& error)
                                     {
                                         synchronize_aliases();
                                         return prepare_light(world_, node_aliases_, arguments,
                                                              result, edit, error);
                                     }) &&
           commands.register_command("set_material_type", true,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>& edit, std::string& error)
                                     {
                                         synchronize_aliases();
                                         return prepare_material_type(
                                             world_, node_aliases_, arguments, result, edit, error);
                                     }) &&
           commands.register_command("set_material_parameter", true,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>& edit, std::string& error)
                                     {
                                         synchronize_aliases();
                                         return prepare_material(world_, node_aliases_, arguments,
                                                                 result, edit, error);
                                     }) &&
           commands.register_command("set_camera", true,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>& edit, std::string& error)
                                     {
                                         synchronize_aliases();
                                         return prepare_camera(world_, node_aliases_, arguments,
                                                               result, edit, error);
                                     }) &&
           commands.register_command("set_material_texture", true,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>& edit, std::string& error)
                                     {
                                         synchronize_aliases();
                                         return prepare_texture(world_, node_aliases_, arguments,
                                                                result, edit, error);
                                     });
}

} // namespace engine::editor
