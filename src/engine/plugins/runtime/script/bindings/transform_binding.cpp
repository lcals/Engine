#include "plugins/runtime/script/bindings/transform_binding.hpp"
#include "plugins/runtime/script/bindings/binding_helpers.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/transform.hpp"

#include <cmath>

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Transform extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.TransformKind); }
    SetPosition(x, y, z) {
        const h = handles.get(this); return native.TransformPosition(h.scene, h.node, x, y, z);
    }
    SetRotation(x, y, z) {
        const h = handles.get(this); return native.TransformRotation(h.scene, h.node, x, y, z);
    }
    SetScale(x, y, z) {
        const h = handles.get(this); return native.TransformScale(h.scene, h.node, x, y, z);
    }
    GetWorldPosition() {
        const h = handles.get(this); return native.TransformWorldPosition(h.scene, h.node);
    }
    GetWorldForward() {
        const h = handles.get(this); return native.TransformWorldForward(h.scene, h.node);
    }
}
componentTypes.set(Transform, { add: native.TransformAdd, kind: native.TransformKind });
Object.freeze(Transform.prototype);
Object.freeze(Transform);
)js";

void set_vector(const Arguments& arguments, Vec3 TransformSettings::* field)
{
    arguments.set_return(false);
    auto node = BindingContext::node(arguments);
    auto transform = !node ? Transform{} : node.get_component<Transform>();
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (!transform || arguments.count() != 5 || !BindingContext::number(arguments, 2, x) ||
        !BindingContext::number(arguments, 3, y) || !BindingContext::number(arguments, 4, z))
    {
        return;
    }
    auto settings = transform.settings();
    settings.*field = Vec3(x, y, z);
    arguments.set_return(transform.set_settings(settings));
}

void position(const Arguments& arguments)
{
    set_vector(arguments, &TransformSettings::position);
}
void rotation(const Arguments& arguments)
{
    set_vector(arguments, &TransformSettings::rotation);
}
void scale(const Arguments& arguments)
{
    set_vector(arguments, &TransformSettings::scale);
}

void world_vector(const Arguments& arguments)
{
    arguments.set_return({});
    const auto node = BindingContext::node(arguments);
    const auto transform = node ? node.get_component<Transform>() : Transform{};
    float matrix[16]{};
    if (!transform || arguments.count() != 2 || !transform.world_matrix(matrix))
    {
        return;
    }
    double x = matrix[12];
    double y = matrix[13];
    double z = matrix[14];
    if (arguments.data() != 0)
    {
        x = -matrix[8];
        y = -matrix[9];
        z = -matrix[10];
        const double length = std::sqrt(x * x + y * y + z * z);
        if (!std::isfinite(length) || length < 1e-9)
        {
            return;
        }
        x /= length;
        y /= length;
        z /= length;
    }
    arguments.set_return(vm::NativeValue::Object{{"x", x}, {"y", y}, {"z", z}});
}

} // namespace

std::string_view TransformBinding::name() const noexcept
{
    return "Transform";
}

std::string_view TransformBinding::source() const noexcept
{
    return javascript;
}

bool TransformBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("TransformKind", static_cast<int>(Transform::component_type)) &&
           vm.function("TransformAdd", binding_detail::add_component<Transform>) &&
           vm.function("TransformPosition", position) &&
           vm.function("TransformRotation", rotation) && vm.function("TransformScale", scale) &&
           vm.function("TransformWorldPosition", world_vector, 0) &&
           vm.function("TransformWorldForward", world_vector, 1);
}

} // namespace engine::plugins::script::bindings
