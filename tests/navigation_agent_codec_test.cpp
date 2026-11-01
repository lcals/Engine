#include "plugins/editor/navigation_crowd/navigation_agent_codec.hpp"
#include "plugins/runtime/navigation_crowd/navigation_agent.hpp"

#include <cstdio>
#include <limits>
#include <string>

namespace
{

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "navigation agent codec test: %s\n", message);
    }
    return condition;
}

bool roundtrip()
{
    namespace plugin = engine::plugins::navigation_crowd;
    namespace pipeline = engine::editor::asset_pipeline;
    if (!expect(plugin::register_navigation_agent_codec() &&
                    plugin::register_navigation_agent_codec(),
                "explicit codec registration is repeatable"))
    {
        return false;
    }
    const auto* codec = pipeline::find_scene_component_codec("navigation_agent");
    if (!expect(codec != nullptr && codec == pipeline::find_scene_component_codec(
                                                 plugin::NavigationAgent::component_type),
                "codec is registered by source name and generated type"))
    {
        return false;
    }
    engine::schema::NavigationAgentDataT settings;
    settings.radius = .45F;
    settings.height = 2.0F;
    settings.max_speed = 4.0F;
    settings.max_acceleration = 12.0F;
    settings.separation_weight = 3.0F;
    settings.arrival_distance = .25F;
    settings.face_velocity = false;
    engine::schema::ComponentDataT source;
    source.payload.Set(settings);
    toml::table table;
    codec->write(source, table);
    engine::schema::ComponentDataT output;
    std::string error;
    if (!expect(codec->validate(source, error) && codec->read(table, output, error) &&
                    output == source,
                "TOML roundtrip preserves every generated navigation setting") ||
        !expect(table.size() == 8 && !table.contains("target") && !table.contains("state") &&
                    !table.contains("velocity"),
                "runtime movement state and identity are not persisted"))
    {
        return false;
    }
    for (const auto name : {"radius", "height", "max_speed", "max_acceleration",
                            "separation_weight", "arrival_distance"})
    {
        for (double invalid : {-1.0, std::numeric_limits<double>::infinity()})
        {
            auto rejected = table;
            rejected.insert_or_assign(name, invalid);
            error.clear();
            if (!expect(!codec->read(rejected, output, error) && output == source && !error.empty(),
                        "invalid scalar is rejected without changing the previous payload"))
            {
                return false;
            }
        }
    }
    auto rejected = table;
    rejected.insert_or_assign("face_velocity", "false");
    if (!expect(!codec->read(rejected, output, error) && output == source,
                "boolean strings are rejected transactionally"))
    {
        return false;
    }
    rejected = table;
    rejected.insert_or_assign("radius", "0.3");
    if (!expect(!codec->read(rejected, output, error) && output == source,
                "numeric strings are rejected transactionally"))
    {
        return false;
    }
    engine::schema::ComponentDataT defaults;
    defaults.payload.Set(engine::schema::NavigationAgentDataT{});
    if (!expect(codec->read(toml::table{}, output, error) && output == defaults,
                "omitted source fields use schema defaults"))
    {
        return false;
    }
    engine::schema::ComponentDataT invalid;
    invalid.payload.Set(engine::schema::CameraDataT{});
    return expect(!codec->validate(invalid, error), "foreign component payload is rejected");
}

} // namespace

int main()
{
    return roundtrip() ? 0 : 1;
}
