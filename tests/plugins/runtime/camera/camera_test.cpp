#include "control_fixture.hpp"
#include <cmath>
using namespace test_support;
using namespace engine;
namespace
{
bool transitions_and_identity()
{
    ControlFixture f;
    if (!f.initialize(false))
    {
        return false;
    }
    auto c = f.view.get_component<camera::CameraController>();
    auto d = camera_definition();
    schema::CameraParameterDataT trigger;
    trigger.name = "go";
    trigger.type = schema::CameraParameterType::trigger;
    trigger.default_value = std::make_unique<schema::CameraParameterValueT>();
    trigger.default_value->name = "go";
    trigger.default_value->type = trigger.type;
    d.parameters.push_back(std::move(trigger));
    auto rig = d.rigs[0];
    rig.id = "other";
    rig.modules[1].id = 3;
    d.rigs.push_back(rig);
    auto fixed = d.module_data->fixed[0];
    fixed.id = 3;
    fixed.position = {10, 2, 4};
    fixed.anchor_slot = "destination";
    schema::CameraTargetDataT destination;
    destination.name = "destination";
    d.targets.push_back(destination);
    d.module_data->fixed.push_back(fixed);
    schema::CameraStateDataT state;
    state.id = "other";
    state.rig = "other";
    d.machine->states.push_back(state);
    d.machine->condition_data = std::make_unique<schema::CameraConditionSetDataT>();
    schema::CameraTriggerTestDataT condition;
    condition.id = 1;
    condition.parameter = "go";
    d.machine->condition_data->trigger_tests.push_back(condition);
    schema::CameraTransitionDataT edge;
    edge.id = "go";
    edge.from = "idle";
    edge.to = "other";
    edge.blend = std::make_unique<schema::CameraBlendDataT>();
    edge.blend->seconds = .2F;
    schema::CameraConditionGroupDataT group;
    schema::CameraConditionRefT ref;
    ref.id = 1;
    ref.type = "trigger";
    group.all_of.push_back(ref);
    edge.any_of.push_back(group);
    d.machine->transitions.push_back(edge);
    std::string error;
    if (!c.set_settings(d, error) || !f.cameras.tick(0) || !c.trigger("go") || !f.cameras.tick(0))
    {
        return check(false, error.c_str());
    }
    camera::CameraStatus status;
    schema::CameraParameterValueT value;
    if (!c.get_status(status) || !c.get_parameter("go", value))
    {
        return false;
    }
    if (!check(status.state == "idle" && value.bool_value && status.has_output &&
                   std::abs(status.output.position.x()) < .01,
               "failed destination preserves current state, pose and trigger"))
    {
        return false;
    }
    auto anchor = f.world.scene()->create_node("Destination");
    if (!c.set_target("destination", anchor) || !f.cameras.tick(0) || !c.get_status(status) ||
        !c.get_parameter("go", value))
    {
        return false;
    }
    if (!check(status.state == "other" && status.run_state == camera::CameraRunState::blending &&
                   !value.bool_value && std::abs(status.output.position.x()) < .01,
               "selected transition consumes trigger only after output commit"))
    {
        return false;
    }
    if (!f.cameras.tick(.1) || !c.get_status(status) ||
        !check(std::abs(status.output.position.x() - 5) < .01, "camera blends fixed poses"))
    {
        return false;
    }
    if (!f.cameras.tick(.2) || !c.get_status(status) ||
        !check(std::abs(status.output.position.x() - 10) < .01, "camera reaches destination"))
    {
        return false;
    }
    d = camera_definition(f.player.local_id());
    if (!c.set_settings(d, error))
    {
        return false;
    }
    auto original = d;
    schema::CameraParameterValueT values[2];
    values[0].name = "yaw";
    values[0].float_value = 45;
    values[1].name = "unknown";
    if (!check(!c.set_parameters(values) && c.get_parameter("yaw", value) && value.float_value == 0,
               "parameter batch is atomic"))
    {
        return false;
    }
    if (!check(!c.set_target("subject", f.view), "self target is rejected"))
    {
        return false;
    }
    auto target = f.world.scene()->create_node("Target");
    auto t = target.get_component<Transform>();
    auto pose = read_settings(t);
    pose.position = {20, 0, 0};
    if (!t.set_settings(pose) || !c.set_target("subject", target) || !f.cameras.tick(0))
    {
        return false;
    }
    Node resolved;
    if (!c.get_target("subject", resolved) ||
        !check(resolved == target, "explicit target override"))
    {
        return false;
    }
    const auto last = read_settings(f.view.get_component<Transform>());
    if (!f.world.scene()->destroy_node(target.id()) || !f.cameras.tick(.1) ||
        !c.get_target("subject", resolved))
    {
        return false;
    }
    return check(!resolved && read_settings(f.view.get_component<Transform>()) == last,
                 "destroyed target never rebinds; committed pose is preserved");
}
bool constraints_preserve_pose()
{
    ControlFixture f;
    if (!f.initialize(false))
    {
        return false;
    }
    auto c = f.view.get_component<camera::CameraController>();
    auto d = camera_definition();
    schema::CameraBoundsDataT a;
    a.id = 5;
    a.minimum = {-1, -1, -1};
    a.maximum = {1, 1, 1};
    schema::CameraBoundsDataT b = a;
    b.id = 6;
    b.minimum = {10, 10, 10};
    b.maximum = {11, 11, 11};
    d.module_data->bounds = {a, b};
    schema::CameraModuleRefT ref;
    ref.id = 5;
    ref.type = "bounds";
    d.output_modules.push_back(ref);
    ref.id = 6;
    d.output_modules.push_back(ref);
    std::string error;
    const auto pose = read_settings(f.view.get_component<Transform>());
    if (!c.set_settings(d, error) || !f.cameras.tick(.1))
    {
        return false;
    }
    camera::CameraStatus status;
    if (!c.get_status(status))
    {
        return false;
    }
    return check(status.run_state == camera::CameraRunState::blocked &&
                     read_settings(f.view.get_component<Transform>()) == pose,
                 "all final constraints are checked jointly before pose commit");
}
bool collision_and_rotation()
{
    ControlFixture f;
    if (!f.initialize(false))
    {
        return false;
    }
    auto c = f.view.get_component<camera::CameraController>();
    auto d = camera_definition(f.player.local_id());
    schema::CameraCollisionDataT collision;
    collision.id = 9;
    collision.subject_slot = "subject";
    collision.subject_offset = {0, 1.5, 0};
    collision.sight_enabled = true;
    collision.motion_enabled = false;
    collision.radius = .2F;
    collision.margin = .02F;
    collision.recovery_delay = .2F;
    collision.recovery_damping = .2F;
    d.module_data->collision.push_back(collision);
    schema::CameraModuleRefT ref;
    ref.id = 9;
    ref.type = "collision";
    d.output_modules.push_back(ref);
    auto wall = f.box("Wall", {0, 1.5, 2.5}, {.7, 1, .2});
    std::string error;
    camera::CameraStatus status;
    if (!wall || !f.physics.tick(0) || !c.set_settings(d, error) || !f.cameras.tick(0) ||
        !c.get_status(status))
    {
        return false;
    }
    if (!check(status.run_state == camera::CameraRunState::ready &&
                   status.output.position.z() > 2 && status.output.position.z() < 2.3,
               "sphere sweep contracts camera before wall"))
    {
        return false;
    }
    const double contracted = status.output.position.z();
    if (!f.world.scene()->destroy_node(wall.id()) || !f.physics.tick(0) || !f.cameras.tick(.05) ||
        !c.get_status(status))
    {
        return false;
    }
    if (!check(std::abs(status.output.position.z() - contracted) < .001,
               "collision recovery delay holds safe distance"))
    {
        return false;
    }
    if (!f.cameras.tick(.3) || !c.get_status(status) ||
        !check(status.output.position.z() > contracted && status.output.position.z() < 4,
               "collision recovery is damped"))
    {
        return false;
    }
    auto transform = f.view.get_component<Transform>();
    auto pose = read_settings(transform);
    pose.position = {0, 2, 4};
    pose.rotation = {25, 130, -15};
    float expected[16], actual[16];
    if (!transform.set_settings(pose) || !transform.world_matrix(expected))
    {
        return false;
    }
    d = camera_definition();
    d.module_data->fixed[0].rotation_degrees = pose.rotation;
    pose.rotation = {};
    if (!transform.set_settings(pose) || !c.set_settings(d, error) || !f.cameras.tick(0) ||
        !transform.world_matrix(actual))
    {
        return false;
    }
    for (int i = 0; i < 16; ++i)
    {
        if (!check(std::abs(expected[i] - actual[i]) < .0001,
                   "quaternion output matches scene Euler convention beyond 90 degrees"))
        {
            return false;
        }
    }
    return true;
}
} // namespace
int main()
{
    return transitions_and_identity() && constraints_preserve_pose() && collision_and_rotation()
               ? 0
               : 1;
}
