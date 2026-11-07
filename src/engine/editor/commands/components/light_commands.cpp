#include "editor/commands/components/component_edit.hpp"

#include "runtime/scene/light.hpp"

#include <algorithm>

namespace engine::editor::components
{
namespace
{
constexpr std::uint64_t color_field = std::uint64_t{1} << 0;
constexpr std::uint64_t intensity_field = std::uint64_t{1} << 1;
constexpr std::uint64_t kind_field = std::uint64_t{1} << 2;
constexpr std::uint64_t range_field = std::uint64_t{1} << 3;
constexpr std::uint64_t inner_angle_field = std::uint64_t{1} << 4;
constexpr std::uint64_t outer_angle_field = std::uint64_t{1} << 5;
constexpr std::uint64_t cast_shadows_field = std::uint64_t{1} << 6;
constexpr std::uint64_t shadow_resolution_field = std::uint64_t{1} << 7;
constexpr std::uint64_t shadow_bias_field = std::uint64_t{1} << 8;

[[nodiscard]] bool change_light(const Target& target, const LightSettings& expected,
                                const LightSettings& replacement, std::uint64_t selected,
                                std::string& error)
{
    auto node = target.node(error);
    auto component = node.valid() ? node.get_component<Light>() : Light{};
    if (!component)
    {
        error = "Light target no longer exists";
        return false;
    }
    auto current = component.settings();
    if (((selected & color_field) && !same(current.color, expected.color)) ||
        ((selected & intensity_field) && current.intensity != expected.intensity) ||
        ((selected & kind_field) && current.kind != expected.kind) ||
        ((selected & range_field) && current.range != expected.range) ||
        ((selected & inner_angle_field) && current.inner_angle != expected.inner_angle) ||
        ((selected & outer_angle_field) && current.outer_angle != expected.outer_angle) ||
        ((selected & cast_shadows_field) && current.cast_shadows != expected.cast_shadows) ||
        ((selected & shadow_resolution_field) &&
         current.shadow_resolution != expected.shadow_resolution) ||
        ((selected & shadow_bias_field) && current.shadow_bias != expected.shadow_bias))
    {
        error = "Light changed outside this edit; undo or redo is stale";
        return false;
    }
    if (selected & color_field)
    {
        current.color = replacement.color;
    }
    if (selected & intensity_field)
    {
        current.intensity = replacement.intensity;
    }
    if (selected & kind_field)
    {
        current.kind = replacement.kind;
    }
    if (selected & range_field)
    {
        current.range = replacement.range;
    }
    if (selected & inner_angle_field)
    {
        current.inner_angle = replacement.inner_angle;
    }
    if (selected & outer_angle_field)
    {
        current.outer_angle = replacement.outer_angle;
    }
    if (selected & cast_shadows_field)
    {
        current.cast_shadows = replacement.cast_shadows;
    }
    if (selected & shadow_resolution_field)
    {
        current.shadow_resolution = replacement.shadow_resolution;
    }
    if (selected & shadow_bias_field)
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
    const auto before = component.settings();
    auto after = before;
    std::uint64_t selected = 0;
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
        selected |= color_field;
        after.color = Color(static_cast<float>(values[0]), static_cast<float>(values[1]),
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
        selected |= intensity_field;
        after.intensity = static_cast<float>(value);
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
            after.kind = schema::LightKind::directional;
        }
        else if (value == "point")
        {
            after.kind = schema::LightKind::point;
        }
        else if (value == "spot")
        {
            after.kind = schema::LightKind::spot;
        }
        else
        {
            error = "Unknown light kind";
            return false;
        }
        selected |= kind_field;
    }
    if (arguments.contains("castShadows"))
    {
        if (!arguments["castShadows"].is_boolean())
        {
            error = "castShadows must be boolean";
            return false;
        }
        selected |= cast_shadows_field;
        after.cast_shadows = arguments["castShadows"].get<bool>();
    }
    if (arguments.contains("shadowResolution"))
    {
        const auto& value = arguments["shadowResolution"];
        if (!value.is_number_integer() || value < 128 || value > 8192)
        {
            error = "shadowResolution must be an integer in 128..8192";
            return false;
        }
        selected |= shadow_resolution_field;
        after.shadow_resolution = value.get<std::uint16_t>();
    }
    if (arguments.contains("shadowBias"))
    {
        double value = 0;
        if (!scalar(arguments["shadowBias"], value) || value < 0 || value > .1)
        {
            error = "shadowBias must be a finite value in 0..0.1";
            return false;
        }
        selected |= shadow_bias_field;
        after.shadow_bias = static_cast<float>(value);
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
            selected |= range_field;
            after.range = static_cast<float>(value);
        }
        else if (std::string_view(name) == "innerAngle")
        {
            selected |= inner_angle_field;
            after.inner_angle = static_cast<float>(value);
        }
        else
        {
            selected |= outer_angle_field;
            after.outer_angle = static_cast<float>(value);
        }
    }
    result = {{"sceneId", std::to_string(target.scene_id)}};
    output = std::make_unique<PropertyEdit<schema::LightDataT>>(
        std::move(target), before, std::move(after), selected, change_light);
    return true;
}

} // namespace engine::editor::components
