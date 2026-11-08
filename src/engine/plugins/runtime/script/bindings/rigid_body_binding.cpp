#include "plugins/runtime/script/bindings/rigid_body_binding.hpp"
#include "plugins/runtime/script/bindings/binding_helpers.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/rigid_body.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class RigidBody extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.RigidBodyKind); }
    SetMotion(motion) {
        const h = handles.get(this);
        return native.RigidBodyMotion(h.scene, h.node, motion);
    }
    SetMass(value) {
        const h = handles.get(this);
        return native.RigidBodyMass(h.scene, h.node, value);
    }
    SetVelocity(x, y, z) {
        const h = handles.get(this);
        return native.RigidBodyVelocity(h.scene, h.node, x, y, z);
    }
    GetVelocity() {
        const h = handles.get(this);
        return native.RigidBodyGetVelocity(h.scene, h.node);
    }
    AddImpulse(x, y, z) {
        const h = handles.get(this);
        return native.RigidBodyImpulse(h.scene, h.node, x, y, z);
    }
}
componentTypes.set(RigidBody, { add: native.RigidBodyAdd, kind: native.RigidBodyKind });
Object.freeze(RigidBody.prototype);
Object.freeze(RigidBody);
)js";

void motion(const Arguments& arguments)
{
    auto value = binding_detail::component<RigidBody>(arguments, 3);
    std::string name;
    if (!value || !BindingContext::text(arguments, 2, name))
    {
        return;
    }
    RigidBody::Data settings{};
    if (!value.get_settings(settings))
    {
        arguments.set_return(false);
        return;
    }
    if (name == "static")
    {
        settings.motion = schema::BodyMotion::static_body;
    }
    else if (name == "kinematic")
    {
        settings.motion = schema::BodyMotion::kinematic;
    }
    else if (name == "dynamic")
    {
        settings.motion = schema::BodyMotion::dynamic;
    }
    else
    {
        return;
    }
    arguments.set_return(value.set_settings(settings));
}

void mass(const Arguments& arguments)
{
    auto value = binding_detail::component<RigidBody>(arguments, 3);
    float number = 0.0F;
    if (value.valid() && BindingContext::scalar(arguments, 2, number))
    {
        RigidBody::Data settings{};
        if (!value.get_settings(settings))
        {
            arguments.set_return(false);
            return;
        }
        settings.mass = number;
        arguments.set_return(value.set_settings(settings));
    }
}

void set_vector(const Arguments& arguments, bool impulse)
{
    auto value = binding_detail::component<RigidBody>(arguments, 5);
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (value.valid() && BindingContext::number(arguments, 2, x) &&
        BindingContext::number(arguments, 3, y) && BindingContext::number(arguments, 4, z))
    {
        const Vec3 vector(x, y, z);
        arguments.set_return(impulse ? value.add_impulse(vector)
                                     : value.set_linear_velocity(vector));
    }
}

void velocity(const Arguments& arguments)
{
    set_vector(arguments, false);
}

void impulse(const Arguments& arguments)
{
    set_vector(arguments, true);
}

void get_velocity(const Arguments& arguments)
{
    const auto value = binding_detail::component<RigidBody>(arguments, 2);
    arguments.set_return({});
    if (!value)
    {
        return;
    }
    const auto& vector = value.linear_velocity();
    arguments.set_return(
        vm::NativeValue::Object{{"x", vector.x()}, {"y", vector.y()}, {"z", vector.z()}});
}
} // namespace

std::string_view RigidBodyBinding::name() const noexcept
{
    return "RigidBody";
}

std::string_view RigidBodyBinding::source() const noexcept
{
    return javascript;
}

bool RigidBodyBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("RigidBodyKind", static_cast<int>(RigidBody::component_type)) &&
           vm.function("RigidBodyAdd", binding_detail::add_component<RigidBody>) &&
           vm.function("RigidBodyMotion", motion) && vm.function("RigidBodyMass", mass) &&
           vm.function("RigidBodyVelocity", velocity) &&
           vm.function("RigidBodyGetVelocity", get_velocity) &&
           vm.function("RigidBodyImpulse", impulse);
}
} // namespace engine::plugins::script::bindings
