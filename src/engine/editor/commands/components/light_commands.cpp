#include "editor/commands/components/component_edit.hpp"

#include "runtime/scene/light.hpp"

#include <algorithm>

namespace engine::editor::components
{
namespace
{
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
    bool cast_shadows = false;
    bool shadow_resolution = false;
    bool shadow_bias = false;

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
        auto node = target.node(error);
        auto component = node.valid() ? node.get_component<Light>() : Light{};
        if (!component)
        {
            error = "Light target no longer exists";
            return false;
        }
        auto current = component.settings();
        if ((color && !same(current.color, expected.color)) ||
            (intensity && current.intensity != expected.intensity) ||
            (kind && current.kind != expected.kind) || (range && current.range != expected.range) ||
            (inner_angle && current.inner_angle != expected.inner_angle) ||
            (outer_angle && current.outer_angle != expected.outer_angle) ||
            (cast_shadows && current.cast_shadows != expected.cast_shadows) ||
            (shadow_resolution && current.shadow_resolution != expected.shadow_resolution) ||
            (shadow_bias && current.shadow_bias != expected.shadow_bias))
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
        if (cast_shadows)
        {
            current.cast_shadows = replacement.cast_shadows;
        }
        if (shadow_resolution)
        {
            current.shadow_resolution = replacement.shadow_resolution;
        }
        if (shadow_bias)
        {
            current.shadow_bias = replacement.shadow_bias;
        }
        if (!component.set_settings(current))
        {
            error = "Invalid light values";
            return false;
        }
        return true;
    }
};

} // namespace

bool prepare_light(WorldSystem& world, NodeAliases& aliases, const Json& arguments, Json& result,
                   std::unique_ptr<EditorEdit>& output, std::string& error)
{
    Target target(world, &aliases);
    if (!fields(arguments,
                {"sceneId", "nodeId", "color", "intensity", "kind", "range", "innerAngle",
                 "outerAngle", "castShadows", "shadowResolution", "shadowBias"}) ||
        arguments.size() <= 2 || !target.read(arguments, error))
    {
        if (error.empty())
        {
            error = "set_light requires sceneId, nodeId and light fields";
        }
        return false;
    }
    const auto component = target.node(error).get_component<Light>();
    if (!component)
    {
        error = "Node has no Light";
        return false;
    }
    auto edit = std::make_unique<LightEdit>(target);
    edit->before = component.settings();
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
    if (arguments.contains("castShadows"))
    {
        if (!arguments["castShadows"].is_boolean())
        {
            error = "castShadows must be boolean";
            return false;
        }
        edit->cast_shadows = true;
        edit->after.cast_shadows = arguments["castShadows"].get<bool>();
    }
    if (arguments.contains("shadowResolution"))
    {
        const auto& value = arguments["shadowResolution"];
        if (!value.is_number_integer() || value < 128 || value > 8192)
        {
            error = "shadowResolution must be an integer in 128..8192";
            return false;
        }
        edit->shadow_resolution = true;
        edit->after.shadow_resolution = value.get<std::uint16_t>();
    }
    if (arguments.contains("shadowBias"))
    {
        double value = 0;
        if (!scalar(arguments["shadowBias"], value) || value < 0 || value > .1)
        {
            error = "shadowBias must be a finite value in 0..0.1";
            return false;
        }
        edit->shadow_bias = true;
        edit->after.shadow_bias = static_cast<float>(value);
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

} // namespace engine::editor::components
