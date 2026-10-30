#include "plugins/runtime/script/bindings/rigid_body_binding.hpp"

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

RigidBody* body(const Arguments& arguments, int count)
{
    arguments.GetReturnValue().Set(false);
    auto* node = BindingContext::node(arguments);
    return node != nullptr && arguments.Length() == count ? node->get_component<RigidBody>()
                                                          : nullptr;
}

void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.GetReturnValue().Set(arguments.Length() == 2 && node != nullptr &&
                                   node->add_component<RigidBody>() != nullptr);
}

void motion(const Arguments& arguments)
{
    auto* value = body(arguments, 3);
    std::string name;
    if (value == nullptr || !BindingContext::text(arguments, 2, name))
    {
        return;
    }
    auto settings = value->settings();
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
    arguments.GetReturnValue().Set(value->set_settings(settings));
}

void mass(const Arguments& arguments)
{
    auto* value = body(arguments, 3);
    float number = 0.0F;
    if (value != nullptr && BindingContext::scalar(arguments, 2, number))
    {
        auto settings = value->settings();
        settings.mass = number;
        arguments.GetReturnValue().Set(value->set_settings(settings));
    }
}

void set_vector(const Arguments& arguments, bool impulse)
{
    auto* value = body(arguments, 5);
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (value != nullptr && BindingContext::number(arguments, 2, x) &&
        BindingContext::number(arguments, 3, y) && BindingContext::number(arguments, 4, z))
    {
        const Vec3 vector(x, y, z);
        arguments.GetReturnValue().Set(impulse ? value->add_impulse(vector)
                                               : value->set_linear_velocity(vector));
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
    const auto* value = body(arguments, 2);
    arguments.GetReturnValue().SetNull();
    if (value == nullptr)
    {
        return;
    }
    auto* isolate = arguments.GetIsolate();
    const auto context = isolate->GetCurrentContext();
    const auto output = v8::Object::New(isolate);
    const auto& vector = value->linear_velocity();
    if (BindingContext::define(context, output, "x", v8::Number::New(isolate, vector.x())) &&
        BindingContext::define(context, output, "y", v8::Number::New(isolate, vector.y())) &&
        BindingContext::define(context, output, "z", v8::Number::New(isolate, vector.z())))
    {
        arguments.GetReturnValue().Set(output);
    }
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

bool RigidBodyBinding::register_functions(v8::Local<v8::Context> context,
                                          v8::Local<v8::Object> native) const
{
    return BindingContext::integer(context, native, "RigidBodyKind",
                                   static_cast<int>(RigidBody::component_type)) &&
           BindingContext::function(context, native, "RigidBodyAdd", add) &&
           BindingContext::function(context, native, "RigidBodyMotion", motion) &&
           BindingContext::function(context, native, "RigidBodyMass", mass) &&
           BindingContext::function(context, native, "RigidBodyVelocity", velocity) &&
           BindingContext::function(context, native, "RigidBodyGetVelocity", get_velocity) &&
           BindingContext::function(context, native, "RigidBodyImpulse", impulse);
}
} // namespace engine::plugins::script::bindings
