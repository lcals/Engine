#include "plugins/runtime/script/bindings/character_motor_binding.hpp"
#include "plugins/runtime/script/bindings/binding_helpers.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/physics/character_motor_driver.hpp"
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
    GetStatus() {
        const h = handles.get(this);
        return native.CharacterMotorGetStatus(h.scene, h.node);
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

void capsule(const Arguments& arguments)
{
    auto value = binding_detail::component<CharacterMotor>(arguments, 4);
    float radius = 0;
    float half_height = 0;
    if (value.valid() && BindingContext::scalar(arguments, 2, radius) &&
        BindingContext::scalar(arguments, 3, half_height))
    {
        CharacterMotor::Data settings{};
        if (!value.get_settings(settings))
        {
            arguments.set_return(false);
            return;
        }
        settings.radius = radius;
        settings.half_height = half_height;
        arguments.set_return(value.set_settings(settings));
    }
}

void set_number(const Arguments& arguments, float schema::CharacterMotorDataT::* field)
{
    auto value = binding_detail::component<CharacterMotor>(arguments, 3);
    float number = 0;
    if (value.valid() && BindingContext::scalar(arguments, 2, number))
    {
        CharacterMotor::Data settings{};
        if (!value.get_settings(settings))
        {
            arguments.set_return(false);
            return;
        }
        settings.*field = number;
        arguments.set_return(value.set_settings(settings));
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
    auto value = binding_detail::component<CharacterMotor>(arguments, 4);
    double x = 0;
    double z = 0;
    if (value.valid() && BindingContext::number(arguments, 2, x) &&
        BindingContext::number(arguments, 3, z))
    {
        arguments.set_return(value.set_velocity({x, 0, z}));
    }
}

void get_velocity(const Arguments& arguments)
{
    const auto value = binding_detail::component<CharacterMotor>(arguments, 2);
    arguments.set_return({});
    if (!value)
    {
        return;
    }
    const auto& velocity = value.velocity();
    arguments.set_return(
        vm::NativeValue::Object{{"x", velocity.x()}, {"y", velocity.y()}, {"z", velocity.z()}});
}

void clear_input(const Arguments& arguments)
{
    auto value = binding_detail::component<CharacterMotor>(arguments, 2);
    if (value.valid())
    {
        arguments.set_return(value.clear_input());
    }
}

void get_status(const Arguments& arguments)
{
    const auto motor = binding_detail::component<CharacterMotor>(arguments, 2);
    CharacterMotorStatus s;
    arguments.set_return({});
    if (!motor.get_status(s))
    {
        return;
    }
    using Object = vm::NativeValue::Object;
    const auto vector = [](const Vec3& v) -> vm::NativeValue
    { return Object{{"x", v.x()}, {"y", v.y()}, {"z", v.z()}}; };
    static constexpr const char* ground[] = {"supported", "steep", "unsupported", "airborne"};
    static constexpr const char* shape[] = {"unchanged", "applied", "blocked"};
    arguments.set_return(
        Object{{"step", s.step},
               {"simulationTime", s.simulation_time},
               {"hasSimulated", s.has_simulated},
               {"feetPosition", vector(s.feet_position)},
               {"yawDegrees", s.yaw_degrees},
               {"velocity", vector(s.velocity)},
               {"groundState", std::string(ground[static_cast<int>(s.ground_state)])},
               {"grounded", s.grounded},
               {"support", Object{{"sceneId", s.support.scene_id},
                                  {"nodeId", s.support.node_id},
                                  {"kind", static_cast<std::int32_t>(s.support.kind)},
                                  {"instanceId", s.support.instance_id}}},
               {"supportNormal", vector(s.support_normal)},
               {"supportVelocity", vector(s.support_velocity)},
               {"carriedPlanarVelocity", vector(s.carried_planar_velocity)},
               {"externalPlanarVelocity", vector(s.external_planar_velocity)},
               {"activeHalfHeight", s.active_half_height},
               {"shapeResult", std::string(shape[static_cast<int>(s.shape_result)])},
               {"controlError", s.control_error}});
}

void add_velocity(const Arguments& arguments)
{
    auto value = binding_detail::component<CharacterMotor>(arguments, 5);
    double x = 0;
    double y = 0;
    double z = 0;
    if (value.valid() && BindingContext::number(arguments, 2, x) &&
        BindingContext::number(arguments, 3, y) && BindingContext::number(arguments, 4, z))
    {
        arguments.set_return(value.add_velocity({x, y, z}));
    }
}

void jump(const Arguments& arguments)
{
    auto value = binding_detail::component<CharacterMotor>(arguments, 3);
    float speed = 0;
    if (value.valid() && BindingContext::scalar(arguments, 2, speed))
    {
        arguments.set_return(value.jump(speed));
    }
}

void grounded(const Arguments& arguments)
{
    const auto value = binding_detail::component<CharacterMotor>(arguments, 2);
    arguments.set_return(value.valid() && value.grounded());
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
           vm.function("CharacterMotorAdd", binding_detail::add_component<CharacterMotor>) &&
           vm.function("CharacterMotorCapsule", capsule) &&
           vm.function("CharacterMotorSlope", slope) && vm.function("CharacterMotorStep", step) &&
           vm.function("CharacterMotorSnap", snap) &&
           vm.function("CharacterMotorGravity", gravity) &&
           vm.function("CharacterMotorExternalDamping", external_damping) &&
           vm.function("CharacterMotorVelocity", velocity) &&
           vm.function("CharacterMotorClearInput", clear_input) &&
           vm.function("CharacterMotorAddVelocity", add_velocity) &&
           vm.function("CharacterMotorGetVelocity", get_velocity) &&
           vm.function("CharacterMotorGetStatus", get_status) &&
           vm.function("CharacterMotorJump", jump) &&
           vm.function("CharacterMotorGrounded", grounded);
}

} // namespace engine::plugins::script::bindings
