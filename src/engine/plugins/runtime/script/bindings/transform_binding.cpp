#include "plugins/runtime/script/bindings/transform_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/transform.hpp"

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
}
componentTypes.set(Transform, { add: native.TransformAdd, kind: native.TransformKind });
Object.freeze(Transform.prototype);
Object.freeze(Transform);
)js";

void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node != nullptr &&
                         node->add_component<Transform>() != nullptr);
}

void set_vector(const Arguments& arguments, Vec3 TransformSettings::* field)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    auto* transform = node == nullptr ? nullptr : node->get_component<Transform>();
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (transform == nullptr || arguments.count() != 5 ||
        !BindingContext::number(arguments, 2, x) || !BindingContext::number(arguments, 3, y) ||
        !BindingContext::number(arguments, 4, z))
    {
        return;
    }
    auto settings = transform->settings();
    settings.*field = Vec3(x, y, z);
    arguments.set_return(transform->set_settings(settings));
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
           vm.function("TransformAdd", add) && vm.function("TransformPosition", position) &&
           vm.function("TransformRotation", rotation) && vm.function("TransformScale", scale);
}

} // namespace engine::plugins::script::bindings
