#include "plugins/runtime/script/bindings/collider_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/node.hpp"

#include <string>

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Collider extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.ColliderKind); }
    SetBox(x, y, z) {
        const h = handles.get(this);
        return native.ColliderBox(h.scene, h.node, x, y, z);
    }
    SetSphere(radius) {
        const h = handles.get(this);
        return native.ColliderSphere(h.scene, h.node, radius);
    }
    SetCapsule(radius, halfHeight) {
        const h = handles.get(this);
        return native.ColliderCapsule(h.scene, h.node, radius, halfHeight);
    }
    SetMesh(path) {
        const h = handles.get(this);
        return native.ColliderMesh(h.scene, h.node, path);
    }
    SetFriction(value) {
        const h = handles.get(this);
        return native.ColliderFriction(h.scene, h.node, value);
    }
    SetRestitution(value) {
        const h = handles.get(this);
        return native.ColliderRestitution(h.scene, h.node, value);
    }
    SetTrigger(value) {
        const h = handles.get(this);
        return native.ColliderTrigger(h.scene, h.node, value);
    }
}
componentTypes.set(Collider, { add: native.ColliderAdd, kind: native.ColliderKind });
Object.freeze(Collider.prototype);
Object.freeze(Collider);
)js";

Collider* collider(const Arguments& arguments, int count)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    return node != nullptr && arguments.count() == count ? node->get_component<Collider>()
                                                         : nullptr;
}

void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node != nullptr &&
                         node->add_component<Collider>() != nullptr);
}

void box(const Arguments& arguments)
{
    auto* value = collider(arguments, 5);
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (value != nullptr && BindingContext::number(arguments, 2, x) &&
        BindingContext::number(arguments, 3, y) && BindingContext::number(arguments, 4, z))
    {
        auto settings = value->settings();
        settings.shape = schema::ColliderShape::box;
        settings.mesh_guid.clear();
        settings.half_extents = Vec3(x, y, z);
        arguments.set_return(value->set_settings(settings));
    }
}

void sphere(const Arguments& arguments)
{
    auto* value = collider(arguments, 3);
    float radius = 0.0F;
    if (value != nullptr && BindingContext::scalar(arguments, 2, radius))
    {
        auto settings = value->settings();
        settings.shape = schema::ColliderShape::sphere;
        settings.mesh_guid.clear();
        settings.radius = radius;
        arguments.set_return(value->set_settings(settings));
    }
}

void capsule(const Arguments& arguments)
{
    auto* value = collider(arguments, 4);
    float radius = 0.0F;
    float half_height = 0.0F;
    if (value != nullptr && BindingContext::scalar(arguments, 2, radius) &&
        BindingContext::scalar(arguments, 3, half_height))
    {
        auto settings = value->settings();
        settings.shape = schema::ColliderShape::capsule;
        settings.mesh_guid.clear();
        settings.radius = radius;
        settings.half_height = half_height;
        arguments.set_return(value->set_settings(settings));
    }
}

void mesh(const Arguments& arguments)
{
    auto* value = collider(arguments, 3);
    std::string path;
    if (value != nullptr && BindingContext::text(arguments, 2, path))
    {
        arguments.set_return(value->set_mesh(path));
    }
}

void set_scalar(const Arguments& arguments, float schema::ColliderDataT::* field)
{
    auto* value = collider(arguments, 3);
    float number = 0.0F;
    if (value != nullptr && BindingContext::scalar(arguments, 2, number))
    {
        auto settings = value->settings();
        settings.*field = number;
        arguments.set_return(value->set_settings(settings));
    }
}

void friction(const Arguments& arguments)
{
    set_scalar(arguments, &schema::ColliderDataT::friction);
}

void restitution(const Arguments& arguments)
{
    set_scalar(arguments, &schema::ColliderDataT::restitution);
}

void trigger(const Arguments& arguments)
{
    auto* value = collider(arguments, 3);
    if (value != nullptr && arguments.is_boolean(2))
    {
        auto settings = value->settings();
        settings.is_trigger = arguments.boolean(2);
        arguments.set_return(value->set_settings(settings));
    }
}
} // namespace

std::string_view ColliderBinding::name() const noexcept
{
    return "Collider";
}

std::string_view ColliderBinding::source() const noexcept
{
    return javascript;
}

bool ColliderBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("ColliderKind", static_cast<int>(Collider::component_type)) &&
           vm.function("ColliderAdd", add) && vm.function("ColliderBox", box) &&
           vm.function("ColliderSphere", sphere) && vm.function("ColliderCapsule", capsule) &&
           vm.function("ColliderMesh", mesh) &&
           vm.function("ColliderFriction", friction) &&
           vm.function("ColliderRestitution", restitution) &&
           vm.function("ColliderTrigger", trigger);
}
} // namespace engine::plugins::script::bindings
