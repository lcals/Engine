#include "plugins/runtime/script/bindings/character_motor_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/node.hpp"

namespace engine::plugins::script::bindings
{
namespace
{

constexpr std::string_view javascript = R"js(
export class CharacterMotor extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.CharacterMotorKind); }
    SetCapsule(radius, halfHeight) {
        const h = handles.get(this);
        return native.CharacterMotorCapsule(h.scene, h.node, radius, halfHeight);
    }
    SetSlopeLimit(degrees) {
        const h = handles.get(this);
        return native.CharacterMotorSlope(h.scene, h.node, degrees);
    }
    SetStepHeight(value) {
        const h = handles.get(this);
        return native.CharacterMotorStep(h.scene, h.node, value);
    }
    SetGroundSnapDistance(value) {
        const h = handles.get(this);
        return native.CharacterMotorSnap(h.scene, h.node, value);
    }
    SetGravityFactor(value) {
        const h = handles.get(this);
        return native.CharacterMotorGravity(h.scene, h.node, value);
    }
    SetVelocity(x, z) {
        const h = handles.get(this);
        return native.CharacterMotorVelocity(h.scene, h.node, x, z);
    }
    GetVelocity() {
        const h = handles.get(this);
        return native.CharacterMotorGetVelocity(h.scene, h.node);
    }
    Jump(speed) {
        const h = handles.get(this);
        return native.CharacterMotorJump(h.scene, h.node, speed);
    }
    get grounded() {
        const h = handles.get(this);
        return native.CharacterMotorGrounded(h.scene, h.node);
    }
}
componentTypes.set(CharacterMotor, { add: native.CharacterMotorAdd, kind: native.CharacterMotorKind });
Object.freeze(CharacterMotor.prototype);
Object.freeze(CharacterMotor);
)js";

CharacterMotor* motor(const Arguments& arguments, int count)
{
    arguments.GetReturnValue().Set(false);
    auto* node = BindingContext::node(arguments);
    return node != nullptr && arguments.Length() == count ? node->get_component<CharacterMotor>()
                                                          : nullptr;
}

void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.GetReturnValue().Set(arguments.Length() == 2 && node != nullptr &&
                                   node->add_component<CharacterMotor>() != nullptr);
}

void capsule(const Arguments& arguments)
{
    auto* value = motor(arguments, 4);
    float radius = 0;
    float half_height = 0;
    if (value != nullptr && BindingContext::scalar(arguments, 2, radius) &&
        BindingContext::scalar(arguments, 3, half_height))
    {
        auto settings = value->settings();
        settings.radius = radius;
        settings.half_height = half_height;
        arguments.GetReturnValue().Set(value->set_settings(settings));
    }
}

void set_number(const Arguments& arguments, float schema::CharacterMotorDataT::* field)
{
    auto* value = motor(arguments, 3);
    float number = 0;
    if (value != nullptr && BindingContext::scalar(arguments, 2, number))
    {
        auto settings = value->settings();
        settings.*field = number;
        arguments.GetReturnValue().Set(value->set_settings(settings));
    }
}

void slope(const Arguments& arguments)
{
    set_number(arguments, &schema::CharacterMotorDataT::max_slope_degrees);
}

void step(const Arguments& arguments)
{
    set_number(arguments, &schema::CharacterMotorDataT::step_height);
}

void snap(const Arguments& arguments)
{
    set_number(arguments, &schema::CharacterMotorDataT::ground_snap_distance);
}

void gravity(const Arguments& arguments)
{
    set_number(arguments, &schema::CharacterMotorDataT::gravity_factor);
}

void velocity(const Arguments& arguments)
{
    auto* value = motor(arguments, 4);
    double x = 0;
    double z = 0;
    if (value != nullptr && BindingContext::number(arguments, 2, x) &&
        BindingContext::number(arguments, 3, z))
    {
        arguments.GetReturnValue().Set(value->set_velocity({x, 0, z}));
    }
}

void get_velocity(const Arguments& arguments)
{
    const auto* value = motor(arguments, 2);
    arguments.GetReturnValue().SetNull();
    if (value == nullptr)
    {
        return;
    }
    auto* isolate = arguments.GetIsolate();
    const auto context = isolate->GetCurrentContext();
    const auto output = v8::Object::New(isolate);
    const auto& velocity = value->velocity();
    if (BindingContext::define(context, output, "x", v8::Number::New(isolate, velocity.x())) &&
        BindingContext::define(context, output, "y", v8::Number::New(isolate, velocity.y())) &&
        BindingContext::define(context, output, "z", v8::Number::New(isolate, velocity.z())))
    {
        arguments.GetReturnValue().Set(output);
    }
}

void jump(const Arguments& arguments)
{
    auto* value = motor(arguments, 3);
    float speed = 0;
    if (value != nullptr && BindingContext::scalar(arguments, 2, speed))
    {
        arguments.GetReturnValue().Set(value->jump(speed));
    }
}

void grounded(const Arguments& arguments)
{
    const auto* value = motor(arguments, 2);
    arguments.GetReturnValue().Set(value != nullptr && value->grounded());
}

} // namespace

std::string_view CharacterMotorBinding::name() const noexcept
{
    return "CharacterMotor";
}

std::string_view CharacterMotorBinding::source() const noexcept
{
    return javascript;
}

bool CharacterMotorBinding::register_functions(v8::Local<v8::Context> context,
                                               v8::Local<v8::Object> native) const
{
    return BindingContext::integer(context, native, "CharacterMotorKind",
                                   static_cast<int>(CharacterMotor::component_type)) &&
           BindingContext::function(context, native, "CharacterMotorAdd", add) &&
           BindingContext::function(context, native, "CharacterMotorCapsule", capsule) &&
           BindingContext::function(context, native, "CharacterMotorSlope", slope) &&
           BindingContext::function(context, native, "CharacterMotorStep", step) &&
           BindingContext::function(context, native, "CharacterMotorSnap", snap) &&
           BindingContext::function(context, native, "CharacterMotorGravity", gravity) &&
           BindingContext::function(context, native, "CharacterMotorVelocity", velocity) &&
           BindingContext::function(context, native, "CharacterMotorGetVelocity", get_velocity) &&
           BindingContext::function(context, native, "CharacterMotorJump", jump) &&
           BindingContext::function(context, native, "CharacterMotorGrounded", grounded);
}

} // namespace engine::plugins::script::bindings
