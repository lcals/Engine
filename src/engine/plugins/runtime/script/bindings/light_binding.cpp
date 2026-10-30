#include "plugins/runtime/script/bindings/light_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/node.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Light extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.LightKind); }
    SetDirectional() {
        const h = handles.get(this); return native.LightDirectional(h.scene, h.node);
    }
    SetPoint(range) {
        const h = handles.get(this); return native.LightPoint(h.scene, h.node, range);
    }
    SetSpot(range, inner, outer) {
        const h = handles.get(this); return native.LightSpot(h.scene, h.node, range, inner, outer);
    }
    SetColor(r, g, b) {
        const h = handles.get(this); return native.LightColor(h.scene, h.node, r, g, b);
    }
    SetIntensity(intensity) {
        const h = handles.get(this); return native.LightIntensity(h.scene, h.node, intensity);
    }
}
componentTypes.set(Light, { add: native.LightAdd, kind: native.LightKind });
Object.freeze(Light.prototype);
Object.freeze(Light);
)js";

void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node != nullptr &&
                         node->add_component<Light>() != nullptr);
}

void color(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    auto* light = node == nullptr ? nullptr : node->get_component<Light>();
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    if (light == nullptr || arguments.count() != 5 || !BindingContext::scalar(arguments, 2, r) ||
        !BindingContext::scalar(arguments, 3, g) || !BindingContext::scalar(arguments, 4, b))
    {
        return;
    }
    auto settings = light->settings();
    settings.color = Color(r, g, b, 1.0F);
    arguments.set_return(light->set_settings(settings));
}

void intensity(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    auto* light = node == nullptr ? nullptr : node->get_component<Light>();
    float value = 0.0F;
    if (light != nullptr && arguments.count() == 3 && BindingContext::scalar(arguments, 2, value))
    {
        auto settings = light->settings();
        settings.intensity = value;
        arguments.set_return(light->set_settings(settings));
    }
}
void directional(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    auto* light = node == nullptr ? nullptr : node->get_component<Light>();
    if (light != nullptr && arguments.count() == 2)
    {
        auto value = light->settings();
        value.kind = schema::LightKind::directional;
        arguments.set_return(light->set_settings(value));
    }
}

void point(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    auto* light = node == nullptr ? nullptr : node->get_component<Light>();
    if (light != nullptr && arguments.count() == 3)
    {
        auto value = light->settings();
        value.kind = schema::LightKind::point;
        arguments.set_return(BindingContext::scalar(arguments, 2, value.range) &&
                             light->set_settings(value));
    }
}

void spot(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    auto* light = node == nullptr ? nullptr : node->get_component<Light>();
    if (light != nullptr && arguments.count() == 5)
    {
        auto value = light->settings();
        value.kind = schema::LightKind::spot;
        arguments.set_return(BindingContext::scalar(arguments, 2, value.range) &&
                             BindingContext::scalar(arguments, 3, value.inner_angle) &&
                             BindingContext::scalar(arguments, 4, value.outer_angle) &&
                             light->set_settings(value));
    }
}
} // namespace

std::string_view LightBinding::name() const noexcept
{
    return "Light";
}

std::string_view LightBinding::source() const noexcept
{
    return javascript;
}

bool LightBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("LightKind", static_cast<int>(Light::component_type)) &&
           vm.function("LightAdd", add) && vm.function("LightColor", color) &&
           vm.function("LightIntensity", intensity) &&
           vm.function("LightDirectional", directional) && vm.function("LightPoint", point) &&
           vm.function("LightSpot", spot);
}

} // namespace engine::plugins::script::bindings
