#if defined(ENGINE_HAS_NAVIGATION_CROWD_PLUGIN) && ENGINE_HAS_NAVIGATION_CROWD_PLUGIN

#include "plugins/runtime/script/bindings/navigation_agent_binding.hpp"

#include "plugins/runtime/navigation_crowd/navigation_agent.hpp"
#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/node.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class NavigationAgent extends Component {
    constructor(token, scene, node) {
        super(token, scene, node, native.NavigationAgentKind);
        const h = handles.get(this);
        h.instance = native.NavigationAgentIdentity(scene, node);
        h.validate = () => native.NavigationAgentValid(h.scene, h.node, h.instance);
    }
    Configure(options) {
        if (options === null || typeof options !== 'object' || Array.isArray(options)) return false;
        const h = handles.get(this);
        return native.NavigationAgentConfigure(h.scene, h.node, h.instance,
            options.radius === undefined ? .3 : options.radius,
            options.height === undefined ? 1.8 : options.height,
            options.maxSpeed === undefined ? 3 : options.maxSpeed,
            options.maxAcceleration === undefined ? 8 : options.maxAcceleration,
            options.separationWeight === undefined ? 2 : options.separationWeight,
            options.arrivalDistance === undefined ? .15 : options.arrivalDistance,
            options.faceVelocity === undefined ? true : options.faceVelocity);
    }
    MoveTo(target) {
        if (target === null || typeof target !== 'object' || Array.isArray(target)) return false;
        const h = handles.get(this);
        return native.NavigationAgentMoveTo(h.scene, h.node, h.instance, target.x, target.y, target.z);
    }
    Stop() {
        const h = handles.get(this);
        return native.NavigationAgentStop(h.scene, h.node, h.instance);
    }
    get state() {
        const h = handles.get(this);
        return native.NavigationAgentState(h.scene, h.node, h.instance);
    }
    get velocity() {
        const h = handles.get(this);
        return native.NavigationAgentVelocity(h.scene, h.node, h.instance);
    }
}
componentTypes.set(NavigationAgent, {add: native.NavigationAgentAdd, kind: native.NavigationAgentKind});
Object.freeze(NavigationAgent.prototype);
Object.freeze(NavigationAgent);
)js";

navigation_crowd::NavigationAgent* component(const Arguments& arguments, int count)
{
    auto* node = BindingContext::node(arguments);
    auto* agent =
        node != nullptr ? node->get_component<navigation_crowd::NavigationAgent>() : nullptr;
    std::uint64_t instance = 0;
    return arguments.count() == count && agent != nullptr &&
                   BindingContext::identifier(arguments, 2, instance) &&
                   instance == agent->instance_id()
               ? agent
               : nullptr;
}

void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node != nullptr &&
                         node->add_component<navigation_crowd::NavigationAgent>() != nullptr);
}

void identity(const Arguments& arguments)
{
    arguments.set_return({});
    const auto* node = BindingContext::node(arguments);
    const auto* agent =
        node != nullptr ? node->get_component<navigation_crowd::NavigationAgent>() : nullptr;
    if (arguments.count() == 2 && agent != nullptr)
    {
        arguments.set_return(agent->instance_id());
    }
}

void valid(const Arguments& arguments)
{
    arguments.set_return(component(arguments, 3) != nullptr);
}

void configure(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* agent = component(arguments, 10);
    schema::NavigationAgentDataT settings;
    if (agent == nullptr || !BindingContext::scalar(arguments, 3, settings.radius) ||
        !BindingContext::scalar(arguments, 4, settings.height) ||
        !BindingContext::scalar(arguments, 5, settings.max_speed) ||
        !BindingContext::scalar(arguments, 6, settings.max_acceleration) ||
        !BindingContext::scalar(arguments, 7, settings.separation_weight) ||
        !BindingContext::scalar(arguments, 8, settings.arrival_distance) ||
        !arguments.is_boolean(9))
    {
        return;
    }
    settings.face_velocity = arguments.boolean(9);
    arguments.set_return(agent->set_settings(settings));
}

void move_to(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* agent = component(arguments, 6);
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (agent != nullptr && BindingContext::number(arguments, 3, x) &&
        BindingContext::number(arguments, 4, y) && BindingContext::number(arguments, 5, z))
    {
        arguments.set_return(agent->move_to({x, y, z}));
    }
}

void stop(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* agent = component(arguments, 3);
    if (agent != nullptr)
    {
        agent->stop();
        arguments.set_return(true);
    }
}

void state(const Arguments& arguments)
{
    arguments.set_return({});
    const auto* agent = component(arguments, 3);
    if (agent == nullptr)
    {
        return;
    }
    switch (agent->state())
    {
    case NavigationAgentStatus::idle:
        arguments.set_return("idle");
        break;
    case NavigationAgentStatus::pending:
        arguments.set_return("pending");
        break;
    case NavigationAgentStatus::moving:
        arguments.set_return("moving");
        break;
    case NavigationAgentStatus::arrived:
        arguments.set_return("arrived");
        break;
    case NavigationAgentStatus::unreachable:
        arguments.set_return("unreachable");
        break;
    case NavigationAgentStatus::invalid:
        arguments.set_return("invalid");
        break;
    }
}

void velocity(const Arguments& arguments)
{
    arguments.set_return({});
    const auto* agent = component(arguments, 3);
    if (agent != nullptr)
    {
        const Vec3 value = agent->velocity();
        arguments.set_return(
            vm::NativeValue::Object{{"x", value.x()}, {"y", value.y()}, {"z", value.z()}});
    }
}
} // namespace

std::string_view NavigationAgentBinding::name() const noexcept
{
    return "NavigationAgent";
}

std::string_view NavigationAgentBinding::source() const noexcept
{
    return javascript;
}

bool NavigationAgentBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("NavigationAgentKind",
                      static_cast<int>(navigation_crowd::NavigationAgent::component_type)) &&
           vm.function("NavigationAgentAdd", add) &&
           vm.function("NavigationAgentIdentity", identity) &&
           vm.function("NavigationAgentValid", valid) &&
           vm.function("NavigationAgentConfigure", configure) &&
           vm.function("NavigationAgentMoveTo", move_to) &&
           vm.function("NavigationAgentStop", stop) && vm.function("NavigationAgentState", state) &&
           vm.function("NavigationAgentVelocity", velocity);
}

} // namespace engine::plugins::script::bindings

#endif
