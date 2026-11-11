#include "plugins/runtime/camera/camera_system.hpp"
#include "plugins/runtime/camera/camera_math.hpp"
#include "plugins/runtime/camera/camera_runtime.hpp"
#include "plugins/runtime/camera/camera_state_machine.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/scene/component_runtime.hpp"
#include "runtime/scene/transform.hpp"
namespace engine::plugins::camera
{
namespace
{
void discard_frame(const CameraController::Data& d, CameraControllerRuntime& r, bool triggers)
{
    for (std::size_t i = 0; i < d.parameters.size() && i < r.parameter_values.size(); ++i)
    {
        if (d.parameters[i].lifetime == schema::CameraParameterLifetime::frame ||
            (triggers && d.parameters[i].type == schema::CameraParameterType::trigger))
        {
            r.parameter_values[i] = *d.parameters[i].default_value;
        }
    }
}
const schema::CameraStateDataT* state(const schema::CameraStateMachineDataT& d, std::string_view id)
{
    for (const auto& s : d.states)
    {
        if (s.id == id)
        {
            return &s;
        }
    }
    return nullptr;
}
const schema::CameraTransitionDataT* transition(const schema::CameraStateMachineDataT& d,
                                                std::string_view id)
{
    for (const auto& t : d.transitions)
    {
        if (t.id == id)
        {
            return &t;
        }
    }
    return nullptr;
}
CameraPose blend(const CameraPose& a, const CameraPose& b, double t, schema::CameraBlendCurve curve)
{
    t = std::clamp(t, 0.0, 1.0);
    switch (curve)
    {
    case schema::CameraBlendCurve::smoothstep:
        t = t * t * (3 - 2 * t);
        break;
    case schema::CameraBlendCurve::ease_in:
        t = t * t;
        break;
    case schema::CameraBlendCurve::ease_out:
        t = 1 - (1 - t) * (1 - t);
        break;
    default:
        break;
    }
    CameraPose out = b;
    out.position = detail::add(a.position, detail::mul(detail::sub(b.position, a.position), t));
    out.rotation_xyzw = detail::slerp(a.rotation_xyzw, b.rotation_xyzw, t);
    out.lens.fov_degrees = float(std::lerp(a.lens.fov_degrees, b.lens.fov_degrees, t));
    out.lens.orthographic_size =
        float(std::lerp(a.lens.orthographic_size, b.lens.orthographic_size, t));
    out.lens.near_clip = float(std::lerp(a.lens.near_clip, b.lens.near_clip, t));
    out.lens.far_clip = float(std::lerp(a.lens.far_clip, b.lens.far_clip, t));
    return out;
}
} // namespace
CameraSystem::CameraSystem(WorldSystem& w, PhysicsSystem* p) noexcept : world_(w), physics_(p)
{
}
CameraSystem::~CameraSystem() noexcept
{
    shutdown();
}
bool CameraSystem::on_initialize(const EngineConfig&)
{
    return world_.native_world() != nullptr;
}
void CameraSystem::on_shutdown() noexcept
{
    if (!world_.native_world())
    {
        return;
    }
    const auto* types = camera_types(world_);
    if (!types)
    {
        return;
    }
    auto query =
        flecs::world(world_.native_world()).query_builder<CameraControllerRuntime>().build();
    query.each(
        [&](flecs::entity e, CameraControllerRuntime& r)
        {
            r = {};
            for (const auto& ops : types->modules())
            {
                ops.reset(world_, world_.find_node(e.id()));
            }
        });
}
bool CameraSystem::on_tick(double dt)
{
    if (!std::isfinite(dt) || dt < 0 || !world_.refresh_scene_state())
    {
        return false;
    }
    const auto* types = camera_types(world_);
    if (!types)
    {
        return false;
    }
    for (const auto& ops : types->modules())
    {
        ops.prune(world_);
    }
    auto query = flecs::world(world_.native_world())
                     .query_builder<const ecs_detail::ComponentStorage<CameraController::Data>,
                                    CameraControllerRuntime>()
                     .with<ecs_detail::Published>()
                     .build();
    query.each(
        [&](flecs::entity e, const auto& storage, CameraControllerRuntime& r)
        {
            auto c = world_.find_node(e.id()).get_component<CameraController>();
            if (!c.active())
            {
                discard_frame(storage.data, r, true);
                r.was_active = false;
                r.reseed = true;
                r.status.run_state = CameraRunState::inactive;
                return;
            }
            if (world_.paused())
            {
                discard_frame(storage.data, r, false);
                return;
            }
            if (!r.was_active)
            {
                r.machine = {};
                if (storage.data.machine)
                {
                    r.machine.state_id = storage.data.machine->initial_state;
                }
                r.machine.state_scope = r.machine.state_id + "@0";
                r.reseed = true;
                r.was_active = true;
            }
            static_cast<void>(update(c, dt));
            discard_frame(storage.data, r, false);
        });
    return true;
}
bool CameraSystem::update(CameraController controller, double dt)
{
    const auto* storage = flecs::world(world_.native_world())
                              .entity(controller.node().id())
                              .try_get<ecs_detail::ComponentStorage<CameraController::Data>>();
    auto& r = *engine::ecs_detail::component_runtime(controller);
    const auto& d = storage->data;
    const auto& types = *camera_types(world_);
    const auto fail = [&](CameraRunState status, std::string message)
    {
        for (const auto& ops : types.modules())
        {
            ops.complete(world_, controller, false);
        }
        r.status.run_state = status;
        r.status.diagnostic = std::move(message);
        return false;
    };
    if (!d.machine || d.machine->states.empty())
    {
        return fail(CameraRunState::waiting, "controller is unconfigured");
    }
    auto camera = controller.node().get_component<Camera>();
    auto transform = controller.node().get_component<Transform>();
    TransformSettings original;
    CameraSettings lens;
    if (!camera.active() || !transform.active() || controller.node().parent() ||
        !camera.get_settings(lens) || !transform.get_settings(original) ||
        original.scale != Vec3(1, 1, 1))
    {
        return fail(CameraRunState::error,
                    "camera requires an active root Transform and Camera with unit scale");
    }
    CameraPose initial;
    initial.position = original.position;
    initial.rotation_xyzw = detail::rotation(original.rotation);
    initial.lens = lens;
    CameraEvaluationContext context;
    context.delta_time = dt;
    context.state_time = r.machine.state_seconds;
    context.cut = r.reseed || !r.status.has_output;
    context.physics = physics_;
    context.parameters = r.parameter_values;
    context.target_slots = d.targets;
    context.previous = initial;
    std::vector<CameraTargetPose> targets(d.targets.size());
    for (std::size_t i = 0; i < d.targets.size(); ++i)
    {
        Node node;
        if (!controller.get_target(d.targets[i].name, node))
        {
            return fail(CameraRunState::error, "invalid target slot");
        }
        if (!node)
        {
            continue;
        }
        if (!valid_camera_target(controller.node(), node))
        {
            return fail(CameraRunState::error, "cyclic target dependency");
        }
        float m[16];
        auto t = node.get_component<Transform>();
        if (!t.active() || !t.world_matrix(m))
        {
            continue;
        }
        targets[i].node = node;
        targets[i].position = {m[12], m[13], m[14]};
        for (int col = 0; col < 3; ++col)
        {
            double n = std::sqrt(m[col * 4] * m[col * 4] + m[col * 4 + 1] * m[col * 4 + 1] +
                                 m[col * 4 + 2] * m[col * 4 + 2]);
            if (n <= 1e-9)
            {
                return fail(CameraRunState::error, "singular target transform");
            }
            for (int row = 0; row < 3; ++row)
            {
                m[col * 4 + row] = float(m[col * 4 + row] / n);
            }
        }
        targets[i].rotation_xyzw = detail::from_matrix(m);
    }
    context.targets = targets;
    const auto outgoing = r.outgoing.find(r.machine.state_id);
    if (outgoing != r.outgoing.end())
    {
        context.outgoing_transitions = outgoing->second;
    }
    CameraStateMachine machine;
    CameraTransitionRequest request;
    if (!machine.select(*d.machine, r.machine, context, types, request))
    {
        return fail(CameraRunState::error, "condition evaluation failed");
    }
    auto next = r.machine;
    if (request.selected)
    {
        const auto* edge = transition(*d.machine, request.transition_id);
        const bool interrupted = r.machine.blend_duration > r.machine.blend_seconds;
        machine.commit(request, next);
        next.blend_duration = edge->blend->seconds;
        next.source_snapshot = r.status.has_output ? r.committed_base_pose : initial;
        if (interrupted || edge->source_mode == schema::CameraBlendSource::snapshot)
        {
            next.source_state_id.clear();
            next.source_scope.clear();
        }
    }
    const auto evaluate_rig = [&](std::string_view state_id, std::string_view scope, bool cut,
                                  CameraPose& pose) -> bool
    {
        const auto* s = state(*d.machine, state_id);
        if (!s)
        {
            return false;
        }
        const schema::CameraRigDataT* rig = nullptr;
        for (const auto& v : d.rigs)
        {
            if (v.id == s->rig)
            {
                rig = &v;
                break;
            }
        }
        if (!rig)
        {
            return false;
        }
        auto local = context;
        local.cut = cut;
        local.state_time = next.state_seconds;
        pose = initial;
        for (const auto& ref : rig->modules)
        {
            if (ref.enabled)
            {
                const auto* ops = types.module(ref.type);
                if (!ops || !ops->evaluate(world_, controller, scope, ref.id, d, local, pose))
                {
                    return false;
                }
            }
        }
        return true;
    };
    CameraPose base;
    if (!evaluate_rig(next.state_id, next.state_scope, context.cut || request.selected, base))
    {
        return fail(CameraRunState::waiting,
                    "destination rig cannot resolve its target or parameters");
    }
    double progress = 1;
    if (next.blend_duration > 0)
    {
        CameraPose source = next.source_snapshot;
        if (!next.source_state_id.empty() &&
            !evaluate_rig(next.source_state_id, next.source_scope, context.cut, source))
        {
            return fail(CameraRunState::waiting, "source rig target unavailable");
        }
        next.blend_seconds =
            std::min(next.blend_duration, next.blend_seconds + (request.selected ? 0 : dt));
        progress = next.blend_seconds / next.blend_duration;
        const auto* edge = transition(*d.machine, next.transition_id);
        base = blend(source, base, progress, edge->blend->curve);
        if (progress >= 1)
        {
            next.blend_duration = 0;
            {
                next.source_state_id.clear();
                next.source_scope.clear();
            }
        }
    }
    CameraPose output = base;
    for (const auto& ref : d.output_modules)
    {
        if (ref.enabled)
        {
            const auto* ops = types.module(ref.type);
            if (!ops || !ops->evaluate(world_, controller, "", ref.id, d, context, output))
            {
                return fail(CameraRunState::blocked,
                            "output module " + ref.type + " could not solve");
            }
        }
    }
    for (const auto& ref : d.output_modules)
    {
        if (ref.enabled)
        {
            const auto* ops = types.module(ref.type);
            bool accepted = false;
            if (!ops->check(d, ref.id, context, output, accepted))
            {
                return fail(CameraRunState::error, "constraint query failed");
            }
            if (!accepted)
            {
                return fail(CameraRunState::blocked, "constraints have no common valid pose");
            }
        }
    }
    if (!detail::valid_pose(output))
    {
        return fail(CameraRunState::error, "invalid camera output");
    }
    auto pose = original;
    pose.position = output.position;
    pose.rotation = detail::euler(output.rotation_xyzw);
    if (!camera.set_settings(output.lens))
    {
        return fail(CameraRunState::error, "camera settings rejected");
    }
    if (!transform.set_settings(pose))
    {
        static_cast<void>(camera.set_settings(lens));
        return fail(CameraRunState::error, "camera transform rejected");
    }
    for (const auto& ops : types.modules())
    {
        ops.complete(world_, controller, true);
        ops.retain(world_, controller, next.state_scope, next.source_scope);
    }
    for (const auto& name : request.consumed_triggers)
    {
        for (auto& p : r.parameter_values)
        {
            if (p.name == name)
            {
                p.bool_value = false;
            }
        }
    }
    if (!request.selected)
    {
        next.state_seconds += dt;
    }
    r.machine = std::move(next);
    r.committed_base_pose = base;
    r.reseed = false;
    r.status.has_output = true;
    r.status.output = output;
    r.status.state = r.machine.state_id;
    r.status.source_state = r.machine.source_state_id;
    r.status.rig = state(*d.machine, r.machine.state_id)->rig;
    r.status.state_time = r.machine.state_seconds;
    r.status.blend_progress = float(progress);
    r.status.run_state =
        r.machine.blend_duration > 0 ? CameraRunState::blending : CameraRunState::ready;
    r.status.diagnostic.clear();
    return true;
}
} // namespace engine::plugins::camera
