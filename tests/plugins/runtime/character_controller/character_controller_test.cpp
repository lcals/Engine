#include "control_fixture.hpp"
#include <cmath>
#include <limits>
using namespace test_support;
using namespace engine;
namespace
{
bool movement_and_instances()
{
    ControlFixture f;
    if (!check(f.initialize(), "initialize"))
    {
        return false;
    }
    auto c = f.player.get_component<character::CharacterController>();
    auto motor = f.player.get_component<CharacterMotor>();
    character::CharacterInput move;
    move.move = {1, 1};
    auto invalid = move;
    invalid.actions = {{"unknown", false, true}};
    if (!check(c.set_input(move) && !c.set_input(invalid) && f.controllers.tick(1.0 / 60) &&
                   f.physics.tick(1.0 / 60),
               "invalid input preserves pending intent"))
    {
        return false;
    }
    auto status = f.status();
    if (!check(status.ready && std::hypot(status.desired_planar_velocity.x(),
                                          status.desired_planar_velocity.z()) < 1,
               "normalized movement accelerates at fixed step") ||
        !check(!motor.set_velocity({1, 0, 0}) && !motor.jump(5) && !motor.clear_input(),
               "driver owns direct motor input"))
    {
        return false;
    }
    auto second = f.world.scene()->create_node("Second");
    auto t = second.get_component<Transform>();
    auto pose = read_settings(t);
    pose.position = {5, 0, 0};
    if (!t.set_settings(pose) || !second.add_component<CharacterMotor>() ||
        !second.add_component<character::CharacterController>())
    {
        return false;
    }
    auto npc = second.get_component<character::CharacterController>();
    character::CharacterInput back;
    back.move = {0, -1};
    for (int i = 0; i < 30; ++i)
    {
        if (!npc.set_input(back) || !f.frame(move))
        {
            return false;
        }
    }
    if (!check(read_settings(second.get_component<Transform>()).position.z() > 1 &&
                   read_settings(f.player.get_component<Transform>()).position.z() < -.5,
               "multiple explicit controllers move independently"))
    {
        return false;
    }
    if (!motor.add_velocity({3, 0, 0}) || !c.clear_input() || !f.frame({}))
    {
        return false;
    }
    if (!check(f.status().desired_planar_velocity == Vec3{} &&
                   f.status().motor.external_planar_velocity.x() > 0,
               "clear input preserves external motion"))
    {
        return false;
    }
    return true;
}
bool action_buffer_and_steps()
{
    ControlFixture f;
    if (!f.initialize())
    {
        return false;
    }
    character::CharacterInput press;
    press.actions = {{"jump", true, true}};
    auto before = f.status();
    if (!f.frame(press, 0) ||
        !check(f.status().jump_sequence == before.jump_sequence, "zero-step frame does not jump") ||
        !f.frame({}, 3.0 / 60))
    {
        return false;
    }
    if (!check(f.status().jump_sequence == before.jump_sequence + 1 &&
                   f.status().motor.step == before.motor.step + 3 &&
                   f.status().motor.velocity.y() > 0,
               "one buffered press produces one impulse across substeps"))
    {
        return false;
    }
    if (!f.frame(press) || !f.step({}, 40))
    {
        return false;
    }
    if (!check(f.status().jump_sequence == before.jump_sequence + 1,
               "expired airborne press is not replayed on landing"))
    {
        return false;
    }
    for (int i = 0; i < 90 && !f.status().motor.grounded; ++i)
    {
        if (!f.frame({}))
        {
            return false;
        }
    }
    if (!f.frame(press, 0) || !f.world.set_paused(true) || !f.controllers.tick(1) ||
        !f.physics.tick(1) || !f.world.set_paused(false) || !f.frame({}))
    {
        return false;
    }
    return check(f.status().jump_sequence == before.jump_sequence + 1,
                 "pause discards buffered actions");
}
bool crouch_clearance_and_configuration()
{
    ControlFixture f;
    if (!f.initialize())
    {
        return false;
    }
    character::CharacterInput crouch;
    crouch.move = {0, 1};
    crouch.actions = {{"crouch", true, false}, {"sprint", true, false}};
    if (!f.frame(crouch))
    {
        return false;
    }
    if (!check(f.status().stance == character::CharacterStance::crouched,
               "crouch changes actual shape") ||
        !check(f.status().abilities.size() == 3, "all enabled abilities report decisions"))
    {
        return false;
    }
    auto roof = f.box("Roof", {0, 1.55, 0}, {2, .1, 2});
    if (!roof || !f.frame({}))
    {
        return false;
    }
    if (!check(f.status().stance_blocked &&
                   f.status().stance == character::CharacterStance::crouched,
               "blocked expansion retains crouched capsule"))
    {
        return false;
    }
    auto c = f.player.get_component<character::CharacterController>();
    auto settings = read_settings(c);
    auto old = settings;
    settings.movement->max_speed = -1;
    std::string error;
    if (!check(!c.set_settings(settings, error) && read_settings(c) == old,
               "invalid settings preserve config"))
    {
        return false;
    }
    settings = old;
    settings.abilities.erase(settings.abilities.begin());
    settings.ability_data->crouch.clear();
    if (!c.set_settings(settings, error) || !f.frame({}))
    {
        return false;
    }
    if (!check(f.status().stance_blocked && f.status().desired_planar_velocity == Vec3{},
               "removed stance waits for clearance without active movement"))
    {
        return false;
    }
    if (!roof.set_enabled(false) || !f.frame({}))
    {
        return false;
    }
    return check(f.status().stance == character::CharacterStance::standing &&
                     !f.status().stance_blocked,
                 "restore base capsule after clearance");
}
bool ownership_and_teleport()
{
    ControlFixture f;
    if (!f.initialize())
    {
        return false;
    }
    auto c = f.player.get_component<character::CharacterController>();
    auto m = f.player.get_component<CharacterMotor>();
    character::CharacterInput input;
    input.move = {0, 1};
    input.actions = {{"jump", true, true}};
    if (!f.frame(input, 0) || !m.add_velocity({2, 0, 0}) || !c.set_enabled(false) ||
        !f.controllers.tick(1.0 / 60) || !f.physics.tick(1.0 / 60))
    {
        return false;
    }
    CharacterMotorStatus status;
    if (!m.get_status(status))
    {
        return false;
    }
    if (!check(status.external_planar_velocity.x() > 0 && status.velocity.y() < 1,
               "control loss clears jump and preserves external impulse"))
    {
        return false;
    }
    if (!c.set_enabled(true) || !f.frame({}))
    {
        return false;
    }
    auto t = f.player.get_component<Transform>();
    auto pose = read_settings(t);
    pose.position = {8, 2, 0};
    if (!f.frame(input, 0) || !t.set_settings(pose) || !f.frame({}) || !f.frame({}))
    {
        return false;
    }
    return check(f.status().jump_sequence == 0 && f.status().motor.feet_position.x() > 7,
                 "teleport discards previous input and support history");
}
bool camera_adapter()
{
    ControlFixture f;
    if (!f.initialize())
    {
        return false;
    }
    auto solver = f.view.get_component<camera::CameraController>();
    auto definition = read_settings(solver);
    schema::CameraParameterDataT height;
    height.name = "height";
    height.default_value = std::make_unique<schema::CameraParameterValueT>();
    height.default_value->name = "height";
    height.default_value->float_value = 2;
    definition.parameters.push_back(std::move(height));
    schema::CameraParameterDataT mode;
    mode.name = "first_person";
    mode.type = schema::CameraParameterType::bool_value;
    mode.default_value = std::make_unique<schema::CameraParameterValueT>();
    mode.default_value->name = mode.name;
    mode.default_value->type = mode.type;
    definition.parameters.push_back(std::move(mode));
    auto adapter = f.view.add_component<character::CharacterCamera>();
    auto config = read_settings(adapter);
    config.subject_slot = "subject";
    config.yaw_parameter = "yaw";
    config.pitch_parameter = "pitch";
    config.first_person_parameter = "first_person";
    config.pointer_scale_degrees = {.1, .1};
    config.rate_degrees_per_second = {100, 100};
    schema::CharacterCameraBindingDataT binding;
    binding.source = schema::CharacterCameraSource::active_height;
    binding.parameter = "height";
    binding.offset = -.2F;
    config.status_bindings.push_back(binding);
    auto controller = f.player.get_component<character::CharacterController>();
    auto settings = read_settings(controller);
    settings.camera_node_id = f.view.local_id();
    std::string error;
    if (!solver.set_settings(definition, error) || !adapter.set_settings(config) ||
        !controller.set_settings(settings, error) || !adapter.set_angles(540, 100))
    {
        return false;
    }
    double yaw = 0, pitch = 0;
    if (!adapter.get_angles(yaw, pitch) ||
        !check(yaw == -180 && pitch == 80, "adapter wraps yaw and clamps declared pitch range"))
    {
        return false;
    }
    if (!adapter.set_angles(0, 0) || !adapter.apply_look({10, 0}, {1, 0}, .1) ||
        !adapter.get_angles(yaw, pitch) ||
        !check(std::abs(yaw - 11) < .001, "mouse delta and rate integrate exactly once"))
    {
        return false;
    }
    if (!adapter.toggle_view())
    {
        return false;
    }
    schema::CameraParameterValueT value;
    if (!solver.get_parameter("first_person", value) ||
        !check(value.bool_value, "adapter writes declared mode parameter"))
    {
        return false;
    }
    character::CharacterInput crouch;
    crouch.actions = {{"crouch", true, true}};
    if (!f.frame(crouch) || !solver.get_parameter("height", value))
    {
        return false;
    }
    const auto motor = read_settings(f.player.get_component<CharacterMotor>());
    if (!check(std::abs(value.float_value -
                        (2 * (f.status().motor.active_half_height + motor.radius) - .2)) < .001,
               "camera height follows actual crouched capsule"))
    {
        return false;
    }
    if (!adapter.set_enabled(false) || !f.parameters.tick(0) ||
        !solver.get_parameter("height", value))
    {
        return false;
    }
    if (!check(value.float_value == 2, "lost adapter ownership restores parameter default") ||
        !adapter.set_enabled(true) || !f.frame(crouch) || !solver.get_parameter("height", value))
    {
        return false;
    }
    const auto previous = value.float_value;
    SceneScopeId replacement;
    if (!f.world.begin_scope(SceneLoadKind::replace_scene, 0, replacement) ||
        !f.world.seal_scope(replacement) ||
        f.world.step_scope(replacement, {64, 0.01}) != SceneWorkState::ready ||
        !f.world.publish_scope(replacement))
    {
        return false;
    }
    return check(solver.valid() && !f.world.find_node(f.view.id()) && f.parameters.tick(0) &&
                     solver.get_parameter("height", value) && value.float_value == previous,
                 "retiring cameras drop adapter ownership without writing unpublished data");
}
} // namespace
int main()
{
    return movement_and_instances() && action_buffer_and_steps() &&
                   crouch_clearance_and_configuration() && ownership_and_teleport() &&
                   camera_adapter()
               ? 0
               : 1;
}
