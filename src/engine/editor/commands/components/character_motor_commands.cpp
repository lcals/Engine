#include "editor/commands/components/component_edit.hpp"

#include "runtime/scene/character_motor.hpp"

namespace engine::editor::components
{
namespace
{

bool read(const Json& arguments, schema::CharacterMotorDataT& value, std::string& error)
{
    if (!fields(arguments,
                {"enabled", "radius", "halfHeight", "maxSlopeDegrees", "stepHeight",
                 "groundSnapDistance", "gravityFactor"},
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
           read_number("gravityFactor", value.gravity_factor);
}

Json write(const schema::CharacterMotorDataT& value)
{
    return {{"radius", value.radius},
            {"halfHeight", value.half_height},
            {"maxSlopeDegrees", value.max_slope_degrees},
            {"stepHeight", value.step_height},
            {"groundSnapDistance", value.ground_snap_distance},
            {"gravityFactor", value.gravity_factor}};
}

} // namespace

bool register_character_motor(WorldSystem& world, EditorCommands& commands, NodeAliases& aliases)
{
    return register_component<CharacterMotor, schema::CharacterMotorDataT>(
        world, commands, "character_motor", aliases, read, write);
}

} // namespace engine::editor::components
