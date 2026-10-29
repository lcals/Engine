#include "plugins/runtime/script/bindings/collider_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/node.hpp"

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
    arguments.GetReturnValue().Set(false);
    auto* node = BindingContext::node(arguments);
    return node != nullptr && arguments.Length() == count ? node->get_component<Collider>()
                                                          : nullptr;
}

void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.GetReturnValue().Set(arguments.Length() == 2 && node != nullptr &&
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
        settings.half_extents = Vec3(x, y, z);
        arguments.GetReturnValue().Set(value->set_settings(settings));
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
        settings.radius = radius;
        arguments.GetReturnValue().Set(value->set_settings(settings));
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
        settings.radius = radius;
        settings.half_height = half_height;
        arguments.GetReturnValue().Set(value->set_settings(settings));
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
        arguments.GetReturnValue().Set(value->set_settings(settings));
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
    if (value != nullptr && arguments[2]->IsBoolean())
    {
        auto settings = value->settings();
        settings.is_trigger = arguments[2]->IsTrue();
        arguments.GetReturnValue().Set(value->set_settings(settings));
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

bool ColliderBinding::register_functions(v8::Local<v8::Context> context,
                                         v8::Local<v8::Object> native) const
{
    return BindingContext::integer(context, native, "ColliderKind",
                                   static_cast<int>(Collider::component_type)) &&
           BindingContext::function(context, native, "ColliderAdd", add) &&
           BindingContext::function(context, native, "ColliderBox", box) &&
           BindingContext::function(context, native, "ColliderSphere", sphere) &&
           BindingContext::function(context, native, "ColliderCapsule", capsule) &&
           BindingContext::function(context, native, "ColliderFriction", friction) &&
           BindingContext::function(context, native, "ColliderRestitution", restitution) &&
           BindingContext::function(context, native, "ColliderTrigger", trigger);
}
} // namespace engine::plugins::script::bindings
