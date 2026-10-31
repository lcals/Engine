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
    SetExternalDamping(value) {
        const h = handles.get(this);
        return native.CharacterMotorExternalDamping(h.scene, h.node, value);
    }
    SetVelocity(x, z) {
        const h = handles.get(this);
        return native.CharacterMotorVelocity(h.scene, h.node, x, z);
    }
    ClearInput() {
        const h = handles.get(this);
        return native.CharacterMotorClearInput(h.scene, h.node);
    }
    AddVelocity(x, y, z) {
        const h = handles.get(this);
        return native.CharacterMotorAddVelocity(h.scene, h.node, x, y, z);
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
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    return node != nullptr && arguments.count() == count ? node->get_component<CharacterMotor>()
                                                         : nullptr;
}

void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node != nullptr &&
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
        arguments.set_return(value->set_settings(settings));
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
        arguments.set_return(value->set_settings(settings));
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

void external_damping(const Arguments& arguments)
{
    set_number(arguments, &schema::CharacterMotorDataT::external_damping);
}

void velocity(const Arguments& arguments)
{
    auto* value = motor(arguments, 4);
    double x = 0;
    double z = 0;
    if (value != nullptr && BindingContext::number(arguments, 2, x) &&
        BindingContext::number(arguments, 3, z))
    {
        arguments.set_return(value->set_velocity({x, 0, z}));
    }
}

void get_velocity(const Arguments& arguments)
{
    const auto* value = motor(arguments, 2);
    arguments.set_return({});
    if (value == nullptr)
    {
        return;
    }
    const auto& velocity = value->velocity();
    arguments.set_return(
        vm::NativeValue::Object{{"x", velocity.x()}, {"y", velocity.y()}, {"z", velocity.z()}});
}

void clear_input(const Arguments& arguments)
{
    auto* value = motor(arguments, 2);
    if (value != nullptr)
    {
        value->clear_input();
        arguments.set_return(true);
    }
}

void add_velocity(const Arguments& arguments)
{
    auto* value = motor(arguments, 5);
    double x = 0;
    double y = 0;
    double z = 0;
    if (value != nullptr && BindingContext::number(arguments, 2, x) &&
        BindingContext::number(arguments, 3, y) && BindingContext::number(arguments, 4, z))
    {
        arguments.set_return(value->add_velocity({x, y, z}));
    }
}

void jump(const Arguments& arguments)
{
    auto* value = motor(arguments, 3);
    float speed = 0;
    if (value != nullptr && BindingContext::scalar(arguments, 2, speed))
    {
        arguments.set_return(value->jump(speed));
    }
}

void grounded(const Arguments& arguments)
{
    const auto* value = motor(arguments, 2);
    arguments.set_return(value != nullptr && value->grounded());
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

bool CharacterMotorBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("CharacterMotorKind", static_cast<int>(CharacterMotor::component_type)) &&
           vm.function("CharacterMotorAdd", add) && vm.function("CharacterMotorCapsule", capsule) &&
           vm.function("CharacterMotorSlope", slope) && vm.function("CharacterMotorStep", step) &&
           vm.function("CharacterMotorSnap", snap) &&
           vm.function("CharacterMotorGravity", gravity) &&
           vm.function("CharacterMotorExternalDamping", external_damping) &&
           vm.function("CharacterMotorVelocity", velocity) &&
           vm.function("CharacterMotorClearInput", clear_input) &&
           vm.function("CharacterMotorAddVelocity", add_velocity) &&
           vm.function("CharacterMotorGetVelocity", get_velocity) &&
           vm.function("CharacterMotorJump", jump) &&
           vm.function("CharacterMotorGrounded", grounded);
}

} // namespace engine::plugins::script::bindings
