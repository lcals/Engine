#include "plugins/runtime/character_controller/character_controller_system.hpp"
#include "plugins/runtime/camera/camera_controller.hpp"
#include "plugins/runtime/character_controller/character_camera.hpp"
#include "plugins/runtime/character_controller/character_controller_runtime.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include <cmath>
#include <numbers>
#include <set>
namespace engine::plugins::character_controller
{
namespace
{
const CharacterController::Data* definition(CharacterController c)
{
    if (!c)
    {
        return nullptr;
    }
    const auto* stored = flecs::world(c.node().world()->native_world())
                             .entity(c.node().id())
                             .try_get<ecs_detail::ComponentStorage<CharacterController::Data>>();
    return stored ? &stored->data : nullptr;
}
Vec3 approach(Vec3 from, Vec3 to, double delta)
{
    const double x = to.x() - from.x(), z = to.z() - from.z(), n = std::hypot(x, z);
    if (n <= delta || n == 0)
    {
        return to;
    }
    return {from.x() + x * delta / n, 0, from.z() + z * delta / n};
}
bool look_binding(CharacterController c, CharacterControllerRuntime& r,
                  const CharacterController::Data& d, CharacterCamera& camera)
{
    camera = {};
    if (!d.camera_node_id)
    {
        return false;
    }
    if (!r.camera_resolved)
    {
        r.camera_node = c.node().scene()->find_local_node(d.camera_node_id);
        r.camera_resolved = true;
    }
    camera = r.camera_node.get_component<CharacterCamera>();
    auto solver = r.camera_node.get_component<camera::CameraController>();
    CharacterCamera::Data settings;
    Node target;
    return camera.active() && solver.active() && camera.get_settings(settings) &&
           solver.get_target(settings.subject_slot, target) && target == c.node();
}
} // namespace
CharacterControllerSystem::CharacterControllerSystem(WorldSystem& w) noexcept : world_(w)
{
}
CharacterControllerSystem::~CharacterControllerSystem() noexcept
{
    shutdown();
}
bool CharacterControllerSystem::on_initialize(const EngineConfig&)
{
    return world_.native_world() != nullptr;
}
Component CharacterControllerSystem::control_owner(CharacterMotor motor) const
{
    if (!motor.active() || motor.node().world() != &world_)
    {
        return {};
    }
    auto c = motor.node().get_component<CharacterController>();
    return c.active() ? c : Component{};
}
void CharacterControllerSystem::reset(Component owner, CharacterMotor) noexcept
{
    auto c = CharacterController(owner);
    if (!c || c.node().world() != &world_)
    {
        return;
    }
    if (!c.clear_input())
    {
        return;
    }
    auto& r = *ecs_detail::component_runtime(c);
    r.last_motor_instance = 0;
    r.status.ready = false;
    r.status.diagnostic = "control reset";
    if (const auto* types = character_abilities(world_))
    {
        for (const auto& ops : types->operations())
        {
            ops.reset(world_, c);
        }
    }
}
bool CharacterControllerSystem::on_tick(double dt)
{
    if (!std::isfinite(dt) || dt < 0 || !world_.refresh_scene_state())
    {
        return false;
    }
    const auto* types = character_abilities(world_);
    if (!types)
    {
        return false;
    }
    for (const auto& ops : types->operations())
    {
        ops.prune(world_);
    }
    auto query = flecs::world(world_.native_world())
                     .query_builder<const ecs_detail::ComponentStorage<CharacterController::Data>,
                                    CharacterControllerRuntime>()
                     .with<ecs_detail::Published>()
                     .build();
    query.each(
        [&](flecs::entity e, const auto& stored, CharacterControllerRuntime& r)
        {
            auto c = world_.find_node(e.id()).get_component<CharacterController>();
            if (!c.active() || world_.paused())
            {
                reset(c, {});
                return;
            }
            const auto& d = stored.data;
            r.sampled_input = r.has_input ? std::move(r.pending_input) : CharacterInput{};
            r.pending_input = {};
            r.has_input = false;
            ++r.sequence;
            for (auto& a : r.actions)
            {
                a.held = false;
                for (const auto& input : r.sampled_input.actions)
                {
                    if (input.name == a.name)
                    {
                        a.held = input.held;
                        if (input.pressed)
                        {
                            a.has_press = true;
                            a.press_sequence = r.sequence;
                            a.press_age = 0;
                        }
                    }
                }
            }
            r.status.input_sequence = r.sequence;
            r.status.actions = r.actions;
            CharacterCamera camera;
            const bool bound = look_binding(c, r, d, camera);
            const bool needs_view =
                d.movement && (d.movement->space == schema::CharacterMoveSpace::view ||
                               (d.facing && d.facing->mode == schema::CharacterFacingMode::view));
            double pitch = 0;
            if ((needs_view && !bound) ||
                (bound &&
                 (!camera.apply_look(r.sampled_input.look_delta, r.sampled_input.look_rate, dt) ||
                  (r.sampled_input.toggle_view && !camera.toggle_view()) ||
                  !camera.get_angles(r.view_yaw, pitch))))
            {
                if (!c.clear_input())
                {
                    return;
                }
                r.status.ready = false;
                r.status.diagnostic = "invalid character camera binding";
                return;
            }
            r.status.ready = static_cast<bool>(c.node().get_component<CharacterMotor>());
            r.status.diagnostic = r.status.ready ? "" : "missing character motor";
        });
    return true;
}
bool CharacterControllerSystem::prepare_step(Component owner, CharacterMotor motor,
                                             const CharacterMotorStepContext& context,
                                             CharacterMotorCommand& out)
{
    auto c = CharacterController(owner);
    const auto* d = definition(c);
    const auto* types = character_abilities(world_);
    if (!c.active() || !d || !d->movement || !d->facing || !types || !context.settings ||
        motor.node() != c.node())
    {
        return false;
    }
    auto& r = *ecs_detail::component_runtime(c);
    r.prepared = false;
    r.context = context;
    r.context.settings = nullptr;
    r.step_settings = *context.settings;
    if (!r.status.ready)
    {
        return false;
    }
    if (r.last_motor_instance && r.last_motor_instance != motor.instance_id())
    {
        reset(c, motor);
        return false;
    }
    r.last_motor_instance = motor.instance_id();
    r.context = context;
    r.context.settings = nullptr;
    r.step_settings = *context.settings;
    for (std::size_t i = 0; i < r.actions.size(); ++i)
    {
        if (r.actions[i].has_press && r.actions[i].press_age > 0 &&
            r.actions[i].press_age >= d->actions[i].buffer_seconds)
        {
            r.actions[i].has_press = false;
        }
    }
    const auto& m = *d->movement;
    double yaw = 0;
    if (m.space == schema::CharacterMoveSpace::character)
    {
        yaw = context.before.yaw_degrees;
    }
    else if (m.space == schema::CharacterMoveSpace::view)
    {
        yaw = r.view_yaw;
    }
    yaw *= std::numbers::pi / 180;
    const double x = r.sampled_input.move.x(), z = -r.sampled_input.move.y();
    r.move_amount = static_cast<float>(std::hypot(x, z));
    r.move_direction = r.move_amount > 0
                           ? Vec3((x * std::cos(yaw) + z * std::sin(yaw)) / r.move_amount, 0,
                                  (-x * std::sin(yaw) + z * std::cos(yaw)) / r.move_amount)
                           : Vec3{};
    CharacterAbilityContext ability_context{&context, r.move_direction, r.move_amount, r.actions};
    CharacterMotionPlan plan;
    plan.max_speed = m.max_speed;
    plan.half_height = context.settings->half_height;
    r.requests.assign(d->abilities.size(), {});
    r.decisions.assign(d->abilities.size(), {});
    std::set<std::pair<std::string, std::uint64_t>> presses;
    for (auto index : r.ordered_abilities)
    {
        const auto& ref = d->abilities[index];
        if (!ref.enabled)
        {
            continue;
        }
        const auto* ops = types->find(ref.type);
        auto& request = r.requests[index];
        auto& decision = r.decisions[index];
        if (!ops || !ops->query(world_, c, *d, ref.id, ability_context, plan, request))
        {
            decision.result = CharacterAbilitySelection::failed;
            r.status.diagnostic = "ability query failed: " + ref.id;
            return false;
        }
        if (!request.requested)
        {
            continue;
        }
        if ((static_cast<std::uint32_t>(request.writes) &
             ~static_cast<std::uint32_t>(ops->channels)) != 0)
        {
            decision.result = CharacterAbilitySelection::failed;
            return false;
        }
        if (overlaps(plan.claimed, request.writes) ||
            (!request.consume_action.empty() &&
             presses.contains({request.consume_action, request.press_sequence})))
        {
            decision.result = CharacterAbilitySelection::denied;
            continue;
        }
        decision.result = CharacterAbilitySelection::selected;
        decision.granted_channels = request.writes;
        plan.claimed = plan.claimed | request.writes;
        if (!request.consume_action.empty())
        {
            presses.emplace(request.consume_action, request.press_sequence);
        }
        if (overlaps(request.writes, CharacterControlChannels::speed_policy))
        {
            plan.max_speed = request.values.max_speed;
        }
        if (overlaps(request.writes, CharacterControlChannels::stance))
        {
            plan.half_height = request.values.half_height;
        }
        if (overlaps(request.writes, CharacterControlChannels::vertical_motion))
        {
            plan.vertical_impulse = request.values.vertical_impulse;
            plan.gravity_scale = request.values.gravity_scale;
            plan.detach_support = request.values.detach_support;
        }
        if (overlaps(request.writes, CharacterControlChannels::planar_override))
        {
            plan.planar_override = request.values.planar_override;
        }
        if (overlaps(request.writes, CharacterControlChannels::facing_override))
        {
            plan.yaw_override = request.values.yaw_override;
        }
    }
    // A removed stance ability may need a clearance-checked recovery before active movement
    // resumes.
    const bool recovery =
        context.before.active_half_height < context.settings->half_height - 0.001F &&
        !overlaps(plan.claimed, CharacterControlChannels::stance);
    const double speed =
        (context.before.grounded ? plan.max_speed : std::min(plan.max_speed, m.air_speed_limit)) *
        r.move_amount;
    const Vec3 target(r.move_direction.x() * speed, 0, r.move_direction.z() * speed);
    const double acceleration =
        r.move_amount > 0 ? (context.before.grounded ? m.ground_acceleration : m.air_acceleration)
                          : (context.before.grounded ? m.ground_braking : m.air_braking);
    r.candidate_velocity =
        recovery ? Vec3{} : approach(r.desired_velocity, target, acceleration * context.delta_time);
    out.planar_velocity = overlaps(plan.claimed, CharacterControlChannels::planar_override)
                              ? plan.planar_override
                              : r.candidate_velocity;
    double target_yaw = context.before.yaw_degrees;
    if (d->facing->mode == schema::CharacterFacingMode::view)
    {
        target_yaw = r.view_yaw;
    }
    else if (d->facing->mode == schema::CharacterFacingMode::movement && r.move_amount > 0)
    {
        target_yaw =
            std::atan2(-r.move_direction.x(), -r.move_direction.z()) * 180 / std::numbers::pi;
    }
    double difference = std::remainder(target_yaw - context.before.yaw_degrees, 360);
    out.yaw_degrees = static_cast<float>(
        std::remainder(context.before.yaw_degrees +
                           std::clamp(difference, -d->facing->turn_speed * context.delta_time,
                                      d->facing->turn_speed * context.delta_time),
                       360));
    if (overlaps(plan.claimed, CharacterControlChannels::facing_override))
    {
        out.yaw_degrees = plan.yaw_override;
    }
    out.half_height = plan.half_height;
    out.vertical_impulse = plan.vertical_impulse;
    out.gravity_scale = plan.gravity_scale;
    out.detach_support = plan.detach_support;
    out.inherit_support_planar_velocity = m.inherit_support_planar_velocity;
    r.status.desired_planar_velocity = out.planar_velocity;
    r.prepared = true;
    return true;
}
void CharacterControllerSystem::finish_step(Component owner, CharacterMotor motor,
                                            const CharacterMotorStepResult& result) noexcept
{
    auto c = CharacterController(owner);
    const auto* d = definition(c);
    const auto* types = character_abilities(world_);
    if (!c || !d || !types || c.node() != motor.node())
    {
        return;
    }
    auto& r = *ecs_detail::component_runtime(c);
    auto context = r.context;
    context.settings = &r.step_settings;
    CharacterAbilityContext ability_context{&context, r.move_direction, r.move_amount, r.actions};
    r.status.abilities.clear();
    if (r.prepared && !result.status.control_error)
    {
        r.desired_velocity = r.candidate_velocity;
        for (std::size_t i = 0; i < d->abilities.size(); ++i)
        {
            const auto& ref = d->abilities[i];
            if (!ref.enabled)
            {
                continue;
            }
            auto* ops = types->find(ref.type);
            if (!ops)
            {
                continue;
            }
            bool consume = false;
            CharacterAbilityStatus status;
            ops->finish(world_, c, *d, ref.id, ability_context, r.decisions[i], result, consume,
                        status);
            r.status.abilities.push_back(std::move(status));
            if (consume && r.decisions[i].result == CharacterAbilitySelection::selected)
            {
                for (auto& a : r.actions)
                {
                    if (a.name == r.requests[i].consume_action && a.has_press &&
                        a.press_sequence == r.requests[i].press_sequence)
                    {
                        a.has_press = false;
                        ++r.status.jump_sequence;
                    }
                }
            }
        }
    }
    else
    {
        r.desired_velocity = {};
        r.status.desired_planar_velocity = {};
    }
    if (result.status.grounded && r.status.motor.has_simulated && !r.status.motor.grounded)
    {
        ++r.status.land_sequence;
    }
    r.status.motor = result.status;
    r.status.motion = result.status.grounded           ? CharacterMotionState::grounded
                      : result.status.velocity.y() > 0 ? CharacterMotionState::rising
                                                       : CharacterMotionState::falling;
    r.status.stance = result.status.active_half_height < r.step_settings.half_height - 0.001F
                          ? CharacterStance::crouched
                          : CharacterStance::standing;
    r.status.stance_blocked = result.status.shape_result == CharacterShapeResult::blocked;
    r.status.relative_planar_speed = static_cast<float>(
        std::hypot(result.status.velocity.x() - result.status.support_velocity.x(),
                   result.status.velocity.z() - result.status.support_velocity.z()));
    r.status.ready = !result.status.control_error;
    if (r.status.ready)
    {
        r.status.diagnostic.clear();
    }
    else if (r.status.diagnostic.empty())
    {
        r.status.diagnostic = "motor rejected control request";
    }
    for (auto& a : r.actions)
    {
        if (a.has_press)
        {
            a.press_age += r.context.delta_time;
        }
    }
    r.status.actions = r.actions;
    r.prepared = false;
}
void CharacterControllerSystem::on_shutdown() noexcept
{
    if (!world_.native_world())
    {
        return;
    }
    auto query =
        flecs::world(world_.native_world()).query_builder<CharacterControllerRuntime>().build();
    query.each([&](flecs::entity e, auto&)
               { reset(world_.find_node(e.id()).get_component<CharacterController>(), {}); });
}
} // namespace engine::plugins::character_controller
