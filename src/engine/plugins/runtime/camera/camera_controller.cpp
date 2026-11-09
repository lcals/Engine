#include "plugins/runtime/camera/camera_controller.hpp"
#include "plugins/runtime/camera/camera_math.hpp"
#include "plugins/runtime/camera/camera_runtime.hpp"
#include "runtime/scene/scene.hpp"
#include <set>
namespace engine::plugins::camera
{
bool CameraController::register_type(ComponentRegistry& registry)
{
    return register_component_storage<CameraController, Data, Runtime>(registry);
}
CameraController::Runtime* CameraController::runtime() const noexcept
{
    return runtime_state<Runtime>();
}
bool CameraController::get_settings(Data& out) const
{
    const auto* p = read_data<Data>();
    out = p ? *p : Data{};
    return p != nullptr;
}
bool CameraController::set_settings(const Data& d, std::string& error)
{
    error.clear();
    if (!writable())
    {
        return detail::fail(error, "controller: invalid or read-only");
    }
    const auto* types = camera_types(*node().world());
    if (!types || !valid_settings(d, *types, error))
    {
        return false;
    }
    if (node().parent())
    {
        return detail::fail(error, "controller: camera must be a root node");
    }
    for (const auto& t : d.targets)
    {
        if (t.node_local_id)
        {
            auto target = node().scene()->find_local_node(t.node_local_id);
            if (target && !valid_camera_target(node(), target))
            {
                return detail::fail(error, "targets." + t.name + ": cyclic dependency");
            }
        }
    }
    *write_data<Data>() = d;
    modified();
    return reset();
}
bool CameraController::reset()
{
    if (!writable())
    {
        return false;
    }
    const auto* types = camera_types(*node().world());
    if (!types)
    {
        return false;
    }
    auto& r = *runtime();
    auto previous = r.status;
    const auto revision = r.definition_revision + 1;
    r = {};
    r.definition_revision = revision;
    r.status.has_output = previous.has_output;
    r.status.output = previous.output;
    r.committed_base_pose = previous.output;
    const auto& d = *read_data<Data>();
    for (const auto& p : d.parameters)
    {
        r.parameter_values.push_back(*p.default_value);
    }
    r.default_targets.resize(d.targets.size());
    r.default_resolved.resize(d.targets.size());
    if (d.machine)
    {
        r.machine.state_id = d.machine->initial_state;
        r.machine.state_scope = r.machine.state_id + "@0";
        for (const auto& state : d.machine->states)
        {
            auto& edges = r.outgoing[state.id];
            for (std::size_t i = 0; i < d.machine->transitions.size(); ++i)
            {
                if (d.machine->transitions[i].from.empty() ||
                    d.machine->transitions[i].from == state.id)
                {
                    edges.push_back(i);
                }
            }
            std::stable_sort(edges.begin(), edges.end(),
                             [&](auto a, auto b)
                             {
                                 return d.machine->transitions[a].priority >
                                        d.machine->transitions[b].priority;
                             });
        }
    }
    r.status.run_state = CameraRunState::waiting;
    for (const auto& ops : types->modules())
    {
        ops.reset(*node().world(), node());
    }
    return true;
}
bool CameraController::get_parameter(std::string_view name,
                                     schema::CameraParameterValueT& out) const
{
    out = {};
    if (!valid())
    {
        return false;
    }
    for (const auto& p : runtime()->parameter_values)
    {
        if (p.name == name)
        {
            out = p;
            return true;
        }
    }
    return false;
}
bool CameraController::set_parameters(std::span<const schema::CameraParameterValueT> values)
{
    if (!writable())
    {
        return false;
    }
    std::set<std::string> names;
    const auto& d = *read_data<Data>();
    for (const auto& v : values)
    {
        const auto* p = detail::parameter(d, v.name);
        if (!p || p->type == schema::CameraParameterType::trigger || !names.insert(v.name).second ||
            !valid_parameter_value(*p, v))
        {
            return false;
        }
    }
    for (const auto& v : values)
    {
        for (auto& p : runtime()->parameter_values)
        {
            if (p.name == v.name)
            {
                p = v;
                break;
            }
        }
    }
    return true;
}
bool CameraController::trigger(std::string_view name)
{
    if (!writable())
    {
        return false;
    }
    for (auto& p : runtime()->parameter_values)
    {
        if (p.name == name && p.type == schema::CameraParameterType::trigger)
        {
            p.bool_value = true;
            return true;
        }
    }
    return false;
}
bool CameraController::reset_trigger(std::string_view name)
{
    if (!writable())
    {
        return false;
    }
    for (auto& p : runtime()->parameter_values)
    {
        if (p.name == name && p.type == schema::CameraParameterType::trigger)
        {
            p.bool_value = false;
            return true;
        }
    }
    return false;
}
bool valid_camera_target(Node camera, Node target)
{
    std::set<std::uint64_t> visited;
    std::vector<Node> pending{target};
    while (!pending.empty())
    {
        auto n = pending.back();
        pending.pop_back();
        if (!n)
        {
            continue;
        }
        for (auto ancestor = n; ancestor; ancestor = ancestor.parent())
        {
            if (ancestor == camera)
            {
                return false;
            }
        }
        if (!visited.insert(n.id()).second)
        {
            continue;
        }
        auto c = n.get_component<CameraController>();
        if (!c)
        {
            continue;
        }
        const auto* storage = flecs::world(n.world()->native_world())
                                  .entity(n.id())
                                  .try_get<ecs_detail::ComponentStorage<CameraController::Data>>();
        if (!storage)
        {
            continue;
        }
        for (const auto& slot : storage->data.targets)
        {
            Node value;
            if (c.get_target(slot.name, value) && value)
            {
                pending.push_back(value);
            }
        }
    }
    return true;
}
bool CameraController::set_target(std::string_view slot, Node target)
{
    if (!writable() || !detail::slot(*read_data<Data>(), slot))
    {
        return false;
    }
    if (target.id() != 0 && (!target || target.world() != node().world() ||
                             !flecs::world(target.world()->native_world())
                                  .entity(target.id())
                                  .has<ecs_detail::Published>() ||
                             !valid_camera_target(node(), target)))
    {
        return false;
    }
    runtime()->target_overrides[std::string(slot)] = target;
    runtime()->reseed = true;
    return true;
}
bool CameraController::clear_target_override(std::string_view slot)
{
    if (!writable() || !detail::slot(*read_data<Data>(), slot))
    {
        return false;
    }
    runtime()->target_overrides.erase(std::string(slot));
    runtime()->reseed = true;
    return true;
}
bool CameraController::get_target(std::string_view slot, Node& out) const
{
    out = {};
    if (!valid())
    {
        return false;
    }
    auto& r = *runtime();
    auto it = r.target_overrides.find(slot);
    if (it != r.target_overrides.end())
    {
        if (it->second)
        {
            out = it->second;
        }
        return true;
    }
    const auto& d = *read_data<Data>();
    for (std::size_t i = 0; i < d.targets.size(); ++i)
    {
        if (d.targets[i].name == slot)
        {
            if (i >= r.default_targets.size())
            {
                return false;
            }
            if (!r.default_resolved[i])
            {
                if (!flecs::world(node().world()->native_world())
                         .entity(node().id())
                         .has<ecs_detail::Published>())
                {
                    return true;
                }
                r.default_targets[i] = node().scene()->find_local_node(d.targets[i].node_local_id);
                r.default_resolved[i] = true;
            }
            auto candidate = r.default_targets[i];
            if (candidate)
            {
                out = candidate;
            }
            return true;
        }
    }
    return false;
}
bool CameraController::get_status(CameraStatus& out) const
{
    out = {};
    if (!valid())
    {
        return false;
    }
    out = runtime()->status;
    return true;
}
bool CameraController::restore_data(const schema::ComponentDataT& d)
{
    const auto* value = d.payload.AsCameraControllerData();
    std::string error;
    return d.flags && value && set_settings(*value, error) && restore_flags(d);
}
bool CameraController::remap_node_references(schema::ComponentDataT& d,
                                             std::span<const schema::NodeIdMappingT> mappings) const
{
    auto* value = d.payload.AsCameraControllerData();
    if (!value)
    {
        return false;
    }
    auto targets = value->targets;
    for (auto& t : targets)
    {
        if (t.node_local_id)
        {
            bool found = false;
            for (const auto& map : mappings)
            {
                if (map.source_id == t.node_local_id && !map.retired && map.target_id)
                {
                    t.node_local_id = map.target_id;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                return false;
            }
        }
    }
    value->targets = std::move(targets);
    return true;
}
} // namespace engine::plugins::camera
