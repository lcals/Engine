#pragma once
#include "plugins/runtime/character_controller/character_types.hpp"
#include "runtime/schema/control_json_generated.hpp"
namespace engine::plugins::character_controller::detail
{
inline nlohmann::json motor_status_json(const CharacterMotorStatus& s, bool script = true)
{
    static constexpr const char* ground[] = {"supported", "steep", "unsupported", "airborne"};
    static constexpr const char* shape[] = {"unchanged", "applied", "blocked"};
    const auto v = [](const Vec3& value) { return schema::control_json::write(value, true); };
    return {{"step", schema::control_json::write(s.step, script)},
            {"simulationTime", s.simulation_time},
            {"hasSimulated", s.has_simulated},
            {"feetPosition", v(s.feet_position)},
            {"yawDegrees", s.yaw_degrees},
            {"velocity", v(s.velocity)},
            {"groundState", ground[static_cast<int>(s.ground_state)]},
            {"grounded", s.grounded},
            {"support",
             {{"sceneId", schema::control_json::write(s.support.scene_id, script)},
              {"nodeId", schema::control_json::write(s.support.node_id, script)},
              {"kind", static_cast<int>(s.support.kind)},
              {"instanceId", schema::control_json::write(s.support.instance_id, script)}}},
            {"supportNormal", v(s.support_normal)},
            {"supportVelocity", v(s.support_velocity)},
            {"carriedPlanarVelocity", v(s.carried_planar_velocity)},
            {"externalPlanarVelocity", v(s.external_planar_velocity)},
            {"activeHalfHeight", s.active_half_height},
            {"shapeResult", shape[static_cast<int>(s.shape_result)]},
            {"controlError", s.control_error}};
}
inline nlohmann::json status_json(const CharacterStatus& s, bool script = true)
{
    static constexpr const char* motion[] = {"unknown", "grounded", "rising", "falling"};
    static constexpr const char* stance[] = {"unknown", "standing", "crouched"};
    static constexpr const char* selection[] = {"inactive", "selected", "denied", "failed"};
    nlohmann::json actions = nlohmann::json::array(), abilities = nlohmann::json::array();
    for (const auto& a : s.actions)
    {
        actions.push_back({{"name", a.name},
                           {"held", a.held},
                           {"hasPress", a.has_press},
                           {"pressSequence", schema::control_json::write(a.press_sequence, script)},
                           {"pressAge", a.press_age}});
    }
    for (const auto& a : s.abilities)
    {
        abilities.push_back({{"id", a.id},
                             {"active", a.active},
                             {"phase", a.phase},
                             {"remainingSeconds", a.remaining_seconds},
                             {"selection", selection[static_cast<int>(a.selection)]}});
    }
    return {{"ready", s.ready},
            {"diagnostic", s.diagnostic},
            {"motor", motor_status_json(s.motor, script)},
            {"motion", motion[static_cast<int>(s.motion)]},
            {"stance", stance[static_cast<int>(s.stance)]},
            {"stanceBlocked", s.stance_blocked},
            {"desiredPlanarVelocity", schema::control_json::write(s.desired_planar_velocity, true)},
            {"relativePlanarSpeed", s.relative_planar_speed},
            {"inputSequence", schema::control_json::write(s.input_sequence, script)},
            {"actions", actions},
            {"abilities", abilities},
            {"jumpSequence", schema::control_json::write(s.jump_sequence, script)},
            {"landSequence", schema::control_json::write(s.land_sequence, script)}};
}
} // namespace engine::plugins::character_controller::detail
