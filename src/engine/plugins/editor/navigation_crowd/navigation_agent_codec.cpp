#include "plugins/editor/navigation_crowd/navigation_agent_codec.hpp"

#include "editor/asset_pipeline/toml_helpers.hpp"
#include "plugins/runtime/navigation_crowd/navigation_agent.hpp"

#include <memory>
#include <utility>

namespace engine::plugins::navigation_crowd
{

schema::ComponentPayload NavigationAgentCodec::type() const noexcept
{
    return NavigationAgent::component_type;
}

std::string_view NavigationAgentCodec::name() const noexcept
{
    return "navigation_agent";
}

bool NavigationAgentCodec::validate(const schema::ComponentDataT& component,
                                    std::string& error) const
{
    const auto* value = component.payload.AsNavigationAgentData();
    if (value == nullptr || !NavigationAgent::valid_settings(*value))
    {
        error = "invalid navigation_agent settings";
        return false;
    }
    return true;
}

bool NavigationAgentCodec::read(const toml::table& table, schema::ComponentDataT& component,
                                std::string& error) const
{
    schema::NavigationAgentDataT value;
    const auto read_number = [&](std::string_view name, float& output)
    {
        if (table.contains(name) && !editor::asset_pipeline::float_number(table.get(name), output))
        {
            error = "invalid navigation_agent." + std::string(name);
            return false;
        }
        return true;
    };
    if (!read_number("radius", value.radius) || !read_number("height", value.height) ||
        !read_number("max_speed", value.max_speed) ||
        !read_number("max_acceleration", value.max_acceleration) ||
        !read_number("separation_weight", value.separation_weight) ||
        !read_number("arrival_distance", value.arrival_distance))
    {
        return false;
    }
    if (const auto* node = table.get("face_velocity"); node != nullptr)
    {
        const auto enabled = node->value<bool>();
        if (!enabled)
        {
            error = "invalid navigation_agent.face_velocity";
            return false;
        }
        value.face_velocity = *enabled;
    }
    if (!NavigationAgent::valid_settings(value))
    {
        error = "invalid navigation_agent settings";
        return false;
    }
    component.payload.Set(std::move(value));
    return true;
}

void NavigationAgentCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    const auto* value = component.payload.AsNavigationAgentData();
    table.insert("type", "navigation_agent");
    table.insert("radius", value->radius);
    table.insert("height", value->height);
    table.insert("max_speed", value->max_speed);
    table.insert("max_acceleration", value->max_acceleration);
    table.insert("separation_weight", value->separation_weight);
    table.insert("arrival_distance", value->arrival_distance);
    table.insert("face_velocity", value->face_velocity);
}

bool register_navigation_agent_codec()
{
    if (editor::asset_pipeline::find_scene_component_codec(NavigationAgent::component_type))
    {
        return true;
    }
    std::unique_ptr<editor::asset_pipeline::SceneComponentCodec> codec =
        std::make_unique<NavigationAgentCodec>();
    return editor::asset_pipeline::register_scene_component_codec(codec);
}

} // namespace engine::plugins::navigation_crowd
