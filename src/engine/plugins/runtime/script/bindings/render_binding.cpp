#include "plugins/runtime/script/bindings/render_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/render/render_system.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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
    static SetAmbientTricolor(sky, equator, ground, intensity) {
        if (!Array.isArray(sky) || sky.length !== 3 ||
            !Array.isArray(equator) || equator.length !== 3 ||
            !Array.isArray(ground) || ground.length !== 3) return false;
        return native.RenderAmbientTricolor(...sky, ...equator, ...ground, intensity);
    }
    static SetLinearFog(enabled, r, g, b, start, end) {
        return native.RenderLinearFog(enabled, r, g, b, start, end);
    }
    static SetExponentialFog(enabled, r, g, b, density, squared = false) {
        return native.RenderExponentialFog(enabled, r, g, b, density, squared);
    }
    static SetExposure(value) { return native.RenderExposure(value); }
    static SetFilmicTonemap(enabled) { return native.RenderFilmicTonemap(enabled); }
    static SetHdr(enabled) { return native.RenderHdr(enabled); }
    static SetBloom(enabled, threshold = 1, intensity = 0.15, radius = 1) {
        return native.RenderBloom(enabled, threshold, intensity, radius);
    }
    static SetAmbientOcclusion(enabled, radius = 1, strength = 1, power = 1.5,
                               fadeStart = 80, fadeEnd = 150) {
        return native.RenderAmbientOcclusion(enabled, radius, strength, power, fadeStart, fadeEnd);
    }
    static SetDirectionalShadows(enabled, resolution = 2048, bias = 0.001) {
        return native.RenderDirectionalShadows(enabled, resolution, bias);
    }
    static SetShadowCascades(count, distance, splitLambda = 0.6, blend = 0.1) {
        return native.RenderShadowCascades(count, distance, splitLambda, blend);
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
    settings.ambient_tricolor = false;
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

void ambient_tricolor(const Arguments& arguments)
{
    auto* target = renderer(arguments, 10);
    if (target == nullptr)
    {
        return;
    }
    auto settings = target->settings();
    settings.ambient_tricolor = true;
    const std::array colors = {&settings.ambient_sky_color, &settings.ambient_equator_color,
                               &settings.ambient_ground_color};
    for (std::size_t color = 0; color < colors.size(); ++color)
    {
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            if (!BindingContext::scalar(arguments, static_cast<int>(color * 3 + channel),
                                        (*colors[color])[channel]))
            {
                return;
            }
        }
    }
    if (BindingContext::scalar(arguments, 9, settings.ambient_intensity))
    {
        arguments.set_return(target->set_settings(settings));
    }
}

void linear_fog(const Arguments& arguments)
{
    auto* target = renderer(arguments, 6);
    if (target == nullptr || !arguments.is_boolean(0))
    {
        return;
    }
    auto settings = target->settings();
    settings.fog_mode = arguments.boolean(0) ? FogMode::linear : FogMode::none;
    if (BindingContext::scalar(arguments, 1, settings.fog_color[0]) &&
        BindingContext::scalar(arguments, 2, settings.fog_color[1]) &&
        BindingContext::scalar(arguments, 3, settings.fog_color[2]) &&
        BindingContext::scalar(arguments, 4, settings.fog_start) &&
        BindingContext::scalar(arguments, 5, settings.fog_end))
    {
        arguments.set_return(target->set_settings(settings));
    }
}

void exponential_fog(const Arguments& arguments)
{
    auto* target = renderer(arguments, 6);
    if (target == nullptr || !arguments.is_boolean(0) || !arguments.is_boolean(5))
    {
        return;
    }
    auto settings = target->settings();
    settings.fog_mode = !arguments.boolean(0) ? FogMode::none
                        : arguments.boolean(5) ? FogMode::exponential_squared
                                               : FogMode::exponential;
    if (BindingContext::scalar(arguments, 1, settings.fog_color[0]) &&
        BindingContext::scalar(arguments, 2, settings.fog_color[1]) &&
        BindingContext::scalar(arguments, 3, settings.fog_color[2]) &&
        BindingContext::scalar(arguments, 4, settings.fog_density))
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

void hdr(const Arguments& arguments)
{
    auto* target = renderer(arguments, 1);
    if (target == nullptr || !arguments.is_boolean(0))
    {
        return;
    }
    auto settings = target->settings();
    settings.hdr_enabled = arguments.boolean(0);
    arguments.set_return(target->set_settings(settings));
}

void bloom(const Arguments& arguments)
{
    auto* target = renderer(arguments, 4);
    if (target == nullptr || !arguments.is_boolean(0))
    {
        return;
    }
    auto settings = target->settings();
    settings.bloom_enabled = arguments.boolean(0);
    if (BindingContext::scalar(arguments, 1, settings.bloom_threshold) &&
        BindingContext::scalar(arguments, 2, settings.bloom_intensity) &&
        BindingContext::scalar(arguments, 3, settings.bloom_radius))
    {
        arguments.set_return(target->set_settings(settings));
    }
}

void ambient_occlusion(const Arguments& arguments)
{
    auto* target = renderer(arguments, 6);
    if (target == nullptr || !arguments.is_boolean(0))
    {
        return;
    }
    auto settings = target->settings();
    settings.ao_enabled = arguments.boolean(0);
    if (BindingContext::scalar(arguments, 1, settings.ao_radius) &&
        BindingContext::scalar(arguments, 2, settings.ao_strength) &&
        BindingContext::scalar(arguments, 3, settings.ao_power) &&
        BindingContext::scalar(arguments, 4, settings.ao_fade_start) &&
        BindingContext::scalar(arguments, 5, settings.ao_fade_end))
    {
        arguments.set_return(target->set_settings(settings));
    }
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

void shadow_cascades(const Arguments& arguments)
{
    auto* target = renderer(arguments, 4);
    if (target == nullptr || !arguments.is_uint32(0))
    {
        return;
    }
    const auto count = arguments.unsigned_integer(0);
    if (count != 1 && count != 2 && count != 4)
    {
        return;
    }
    auto settings = target->settings();
    settings.shadow_cascades = static_cast<std::uint8_t>(count);
    if (BindingContext::scalar(arguments, 1, settings.shadow_distance) &&
        BindingContext::scalar(arguments, 2, settings.shadow_split_lambda) &&
        BindingContext::scalar(arguments, 3, settings.shadow_cascade_blend))
    {
        arguments.set_return(target->set_settings(settings));
    }
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
           vm.function("RenderAmbientTricolor", ambient_tricolor) &&
           vm.function("RenderLinearFog", linear_fog) &&
           vm.function("RenderExponentialFog", exponential_fog) &&
           vm.function("RenderFilmicTonemap", filmic_tonemap) &&
           vm.function("RenderHdr", hdr) && vm.function("RenderBloom", bloom) &&
           vm.function("RenderAmbientOcclusion", ambient_occlusion) &&
           vm.function("RenderDirectionalShadows", directional_shadows) &&
           vm.function("RenderShadowCascades", shadow_cascades) &&
           vm.function("RenderClearColor", clear_color);
}

} // namespace engine::plugins::script::bindings
