#include "runtime/script/bindings/light_binding.hpp"

#include "runtime/scene/light.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/script/bindings/binding_context.hpp"

namespace engine::script_bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Light extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.LightKind); }
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
    arguments.GetReturnValue().Set(arguments.Length() == 2 && node != nullptr &&
                                   node->add_component<Light>() != nullptr);
}

void color(const Arguments& arguments)
{
    arguments.GetReturnValue().Set(false);
    auto* node = BindingContext::node(arguments);
    auto* light = node == nullptr ? nullptr : node->get_component<Light>();
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    if (light == nullptr || arguments.Length() != 5 || !BindingContext::scalar(arguments, 2, r) ||
        !BindingContext::scalar(arguments, 3, g) || !BindingContext::scalar(arguments, 4, b))
    {
        return;
    }
    auto settings = light->settings();
    settings.color = Color(r, g, b, 1.0F);
    arguments.GetReturnValue().Set(light->set_settings(settings));
}

void intensity(const Arguments& arguments)
{
    arguments.GetReturnValue().Set(false);
    auto* node = BindingContext::node(arguments);
    auto* light = node == nullptr ? nullptr : node->get_component<Light>();
    float value = 0.0F;
    if (light != nullptr && arguments.Length() == 3 && BindingContext::scalar(arguments, 2, value))
    {
        auto settings = light->settings();
        settings.intensity = value;
        arguments.GetReturnValue().Set(light->set_settings(settings));
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

bool LightBinding::register_functions(v8::Local<v8::Context> context,
                                      v8::Local<v8::Object> native) const
{
    return BindingContext::integer(context, native, "LightKind",
                                   static_cast<int>(Light::component_type)) &&
           BindingContext::function(context, native, "LightAdd", add) &&
           BindingContext::function(context, native, "LightColor", color) &&
           BindingContext::function(context, native, "LightIntensity", intensity);
}

} // namespace engine::script_bindings
