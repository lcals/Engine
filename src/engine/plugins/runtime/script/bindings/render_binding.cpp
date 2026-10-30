#include "plugins/runtime/script/bindings/render_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/render/render_system.hpp"

#include <limits>

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Render {
    constructor() { throw new TypeError('Render is static'); }
    static SetAmbient(r, g, b, intensity) {
        return native.RenderAmbient(r, g, b, intensity);
    }
    static SetExposure(value) { return native.RenderExposure(value); }
    static SetFilmicTonemap(enabled) { return native.RenderFilmicTonemap(enabled); }
    static SetDirectionalShadows(enabled, resolution = 2048, bias = 0.001) {
        return native.RenderDirectionalShadows(enabled, resolution, bias);
    }
    static SetClearColor(rgba) { return native.RenderClearColor(rgba); }
}
Object.freeze(Render.prototype);
Object.freeze(Render);
)js";

RenderSystem* renderer(const Arguments& arguments, int count)
{
    arguments.GetReturnValue().Set(false);
    auto* binding = BindingContext::current(arguments);
    return binding && arguments.Length() == count ? binding->renderer() : nullptr;
}

void ambient(const Arguments& arguments)
{
    auto* target = renderer(arguments, 4);
    if (!target)
    {
        return;
    }
    auto settings = target->settings();
    if (BindingContext::scalar(arguments, 0, settings.ambient_color[0]) &&
        BindingContext::scalar(arguments, 1, settings.ambient_color[1]) &&
        BindingContext::scalar(arguments, 2, settings.ambient_color[2]) &&
        BindingContext::scalar(arguments, 3, settings.ambient_intensity))
    {
        arguments.GetReturnValue().Set(target->set_settings(settings));
    }
}

void exposure(const Arguments& arguments)
{
    auto* target = renderer(arguments, 1);
    if (!target)
    {
        return;
    }
    auto settings = target->settings();
    if (BindingContext::scalar(arguments, 0, settings.exposure))
    {
        arguments.GetReturnValue().Set(target->set_settings(settings));
    }
}

void filmic_tonemap(const Arguments& arguments)
{
    auto* target = renderer(arguments, 1);
    if (!target || !arguments[0]->IsBoolean())
    {
        return;
    }
    auto settings = target->settings();
    settings.filmic_tonemap = arguments[0]->IsTrue();
    arguments.GetReturnValue().Set(target->set_settings(settings));
}

void directional_shadows(const Arguments& arguments)
{
    auto* target = renderer(arguments, 3);
    if (!target || !arguments[0]->IsBoolean() || !arguments[1]->IsUint32())
    {
        return;
    }
    const auto resolution = arguments[1].As<v8::Uint32>()->Value();
    if (resolution > std::numeric_limits<std::uint16_t>::max())
    {
        return;
    }
    auto settings = target->settings();
    settings.directional_shadows = arguments[0]->IsTrue();
    settings.shadow_resolution = static_cast<std::uint16_t>(resolution);
    if (BindingContext::scalar(arguments, 2, settings.shadow_bias))
    {
        arguments.GetReturnValue().Set(target->set_settings(settings));
    }
}

void clear_color(const Arguments& arguments)
{
    auto* target = renderer(arguments, 1);
    if (!target || !arguments[0]->IsUint32())
    {
        return;
    }
    auto settings = target->settings();
    settings.clear_color = arguments[0].As<v8::Uint32>()->Value();
    arguments.GetReturnValue().Set(target->set_settings(settings));
}
} // namespace

std::string_view RenderBinding::name() const noexcept
{
    return "Render";
}

std::string_view RenderBinding::source() const noexcept
{
    return javascript;
}

bool RenderBinding::register_functions(v8::Local<v8::Context> context,
                                       v8::Local<v8::Object> native) const
{
    return BindingContext::function(context, native, "RenderAmbient", ambient) &&
           BindingContext::function(context, native, "RenderExposure", exposure) &&
           BindingContext::function(context, native, "RenderFilmicTonemap", filmic_tonemap) &&
           BindingContext::function(context, native, "RenderDirectionalShadows",
                                    directional_shadows) &&
           BindingContext::function(context, native, "RenderClearColor", clear_color);
}

} // namespace engine::plugins::script::bindings
