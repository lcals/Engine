#include "editor/commands/components/component_edit.hpp"

#include "runtime/scene/character_motor.hpp"

namespace engine::editor::components
{
namespace
{

bool read(const Json& arguments, schema::CharacterMotorDataT& value,
          std::vector<ResourceReference>&, std::string& error)
{
    if (!fields(arguments,
                {"enabled", "radius", "halfHeight", "maxSlopeDegrees", "stepHeight",
                 "groundSnapDistance", "gravityFactor", "externalDamping"},
                error))
    {
        return false;
    }
    const auto read_number = [&](std::string_view name, float& output)
    {
        if (arguments.contains(name) && !number(arguments[name], output))
        {
            error = "Invalid character motor field: " + std::string(name);
            return false;
        }
        return true;
    };
    return read_number("radius", value.radius) && read_number("halfHeight", value.half_height) &&
           read_number("maxSlopeDegrees", value.max_slope_degrees) &&
           read_number("stepHeight", value.step_height) &&
           read_number("groundSnapDistance", value.ground_snap_distance) &&
           read_number("gravityFactor", value.gravity_factor) &&
           read_number("externalDamping", value.external_damping);
}

Json write(const schema::CharacterMotorDataT& value)
{
    return {{"radius", value.radius},
            {"halfHeight", value.half_height},
            {"maxSlopeDegrees", value.max_slope_degrees},
            {"stepHeight", value.step_height},
            {"groundSnapDistance", value.ground_snap_distance},
            {"gravityFactor", value.gravity_factor},
            {"externalDamping", value.external_damping}};
}

} // namespace

bool register_character_motor(WorldSystem& world, EditorCommands& commands,
                              SceneCommands& scene_commands)
{
    return register_component<CharacterMotor, schema::CharacterMotorDataT>(
        world, scene_commands, commands, "character_motor",
        [](Node, const auto& args, auto& data, auto& prepared, auto& error)
        { return read(args, data, prepared, error); },
        [](auto, const auto& data) { return write(data); });
}

} // namespace engine::editor::components
