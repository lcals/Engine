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
    arguments.set_return(false);
    auto* binding = BindingContext::current(arguments);
    return binding && arguments.count() == count ? binding->renderer() : nullptr;
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
        arguments.set_return(target->set_settings(settings));
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
        arguments.set_return(target->set_settings(settings));
    }
}

void filmic_tonemap(const Arguments& arguments)
{
    auto* target = renderer(arguments, 1);
    if (!target || !arguments.is_boolean(0))
    {
        return;
    }
    auto settings = target->settings();
    settings.filmic_tonemap = arguments.boolean(0);
    arguments.set_return(target->set_settings(settings));
}

void directional_shadows(const Arguments& arguments)
{
    auto* target = renderer(arguments, 3);
    if (!target || !arguments.is_boolean(0) || !arguments.is_uint32(1))
    {
        return;
    }
    const auto resolution = arguments.unsigned_integer(1);
    if (resolution > std::numeric_limits<std::uint16_t>::max())
    {
        return;
    }
    auto settings = target->settings();
    settings.directional_shadows = arguments.boolean(0);
    settings.shadow_resolution = static_cast<std::uint16_t>(resolution);
    if (BindingContext::scalar(arguments, 2, settings.shadow_bias))
    {
        arguments.set_return(target->set_settings(settings));
    }
}

void clear_color(const Arguments& arguments)
{
    auto* target = renderer(arguments, 1);
    if (!target || !arguments.is_uint32(0))
    {
        return;
    }
    auto settings = target->settings();
    settings.clear_color = arguments.unsigned_integer(0);
    arguments.set_return(target->set_settings(settings));
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

bool RenderBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.function("RenderAmbient", ambient) && vm.function("RenderExposure", exposure) &&
           vm.function("RenderFilmicTonemap", filmic_tonemap) &&
           vm.function("RenderDirectionalShadows", directional_shadows) &&
           vm.function("RenderClearColor", clear_color);
}

} // namespace engine::plugins::script::bindings
