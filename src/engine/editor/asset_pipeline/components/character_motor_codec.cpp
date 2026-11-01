#include "editor/asset_pipeline/components/character_motor_codec.hpp"
#include "runtime/scene/character_motor.hpp"
#include <utility>
namespace engine::editor::asset_pipeline
{
schema::ComponentPayload CharacterMotorCodec::type() const noexcept
{
    return schema::ComponentPayload::CharacterMotorData;
}
std::string_view CharacterMotorCodec::name() const noexcept
{
    return "character_motor";
}
bool CharacterMotorCodec::validate(const schema::ComponentDataT& component,
                                   std::string& error) const
{
    const auto* value = component.payload.AsCharacterMotorData();
    if (!value || !CharacterMotor::valid_settings(*value))
    {
        error = "invalid character_motor settings";
        return false;
    }
    return true;
}
bool CharacterMotorCodec::read(const toml::table& table, schema::ComponentDataT& component,
                               std::string& error) const
{
    schema::CharacterMotorDataT value;
    if (table.contains("radius") && !float_number(table.get("radius"), value.radius))
    {
        error = "invalid character_motor.radius";
        return false;
    }
    if (table.contains("half_height") && !float_number(table.get("half_height"), value.half_height))
    {
        error = "invalid character_motor.half_height";
        return false;
    }
    if (table.contains("max_slope_degrees") &&
        !float_number(table.get("max_slope_degrees"), value.max_slope_degrees))
    {
        error = "invalid character_motor.max_slope_degrees";
        return false;
    }
    if (table.contains("step_height") && !float_number(table.get("step_height"), value.step_height))
    {
        error = "invalid character_motor.step_height";
        return false;
    }
    if (table.contains("ground_snap_distance") &&
        !float_number(table.get("ground_snap_distance"), value.ground_snap_distance))
    {
        error = "invalid character_motor.ground_snap_distance";
        return false;
    }
    if (table.contains("gravity_factor") &&
        !float_number(table.get("gravity_factor"), value.gravity_factor))
    {
        error = "invalid character_motor.gravity_factor";
        return false;
    }
    if (table.contains("external_damping") &&
        !float_number(table.get("external_damping"), value.external_damping))
    {
        error = "invalid character_motor.external_damping";
        return false;
    }
    if (!CharacterMotor::valid_settings(value))
    {
        error = "invalid character_motor settings";
        return false;
    }
    component.payload.Set(std::move(value));
    return true;
}
void CharacterMotorCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    const auto* value = component.payload.AsCharacterMotorData();
    table.insert("type", "character_motor");
    table.insert("radius", value->radius);
    table.insert("half_height", value->half_height);
    table.insert("max_slope_degrees", value->max_slope_degrees);
    table.insert("step_height", value->step_height);
    table.insert("ground_snap_distance", value->ground_snap_distance);
    table.insert("gravity_factor", value->gravity_factor);
    table.insert("external_damping", value->external_damping);
}
} // namespace engine::editor::asset_pipeline
