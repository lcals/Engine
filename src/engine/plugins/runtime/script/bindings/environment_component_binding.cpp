#include "plugins/runtime/script/bindings/environment_component_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/node.hpp"
#include <array>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class EnvironmentComponent extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.EnvironmentKind); }
    SetAmbient(r, g, b, intensity) {
        const h = handles.get(this);
        return native.EnvironmentAmbient(h.scene, h.node, r, g, b, intensity);
    }
    SetAmbientTricolor(sky, equator, ground, intensity) {
        if (!Array.isArray(sky) || sky.length !== 3 ||
            !Array.isArray(equator) || equator.length !== 3 ||
            !Array.isArray(ground) || ground.length !== 3) return false;
        const h = handles.get(this);
        return native.EnvironmentTricolor(h.scene, h.node, ...sky, ...equator, ...ground, intensity);
    }
    SetLinearFog(enabled, r, g, b, start, end) {
        const h = handles.get(this);
        return native.EnvironmentLinearFog(h.scene, h.node, enabled, r, g, b, start, end);
    }
    SetExponentialFog(enabled, r, g, b, density, squared = false) {
        const h = handles.get(this);
        return native.EnvironmentExponentialFog(h.scene, h.node, enabled, r, g, b, density, squared);
    }
    SetClearColor(rgba) {
        const h = handles.get(this); return native.EnvironmentClearColor(h.scene, h.node, rgba);
    }
    SetAtmosphere(enabled, sun = null, parameters = {}) {
        const h = handles.get(this);
        const other = sun === null ? null : handles.get(sun);
        if (sun !== null && (!(sun instanceof Node) || other?.scene !== h.scene)) return false;
        if (parameters === null || typeof parameters !== "object" || Array.isArray(parameters)) return false;
        return native.EnvironmentAtmosphere(h.scene, h.node, enabled,
            other?.scene ?? 0n, other?.node ?? 0n, JSON.stringify(parameters));
    }
    SetIbl(enabled, intensity = 1) {
        const h = handles.get(this); return native.EnvironmentIbl(h.scene, h.node, enabled, intensity);
    }
}
componentTypes.set(EnvironmentComponent, { add: native.EnvironmentAdd, kind: native.EnvironmentKind });
Object.freeze(EnvironmentComponent.prototype);
Object.freeze(EnvironmentComponent);
)js";

EnvironmentComponent environment(const Arguments& arguments, int count)
{
    arguments.set_return(false);
    auto node = BindingContext::node(arguments);
    return node.valid() && arguments.count() == count ? node.get_component<EnvironmentComponent>()
                                                      : EnvironmentComponent{};
}

bool read_color(const Arguments& arguments, int index, Color& output)
{
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    if (!BindingContext::scalar(arguments, index, r) ||
        !BindingContext::scalar(arguments, index + 1, g) ||
        !BindingContext::scalar(arguments, index + 2, b))
    {
        return false;
    }
    output = Color(r, g, b, 1.0F);
    return true;
}

void add(const Arguments& arguments)
{
    auto node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node.valid() &&
                         node.add_component<EnvironmentComponent>().valid());
}

void ambient(const Arguments& arguments)
{
    auto target = environment(arguments, 6);
    if (!target)
    {
        return;
    }
    auto value = target.settings();
    value.ambient_tricolor = false;
    if (read_color(arguments, 2, value.ambient_color) &&
        BindingContext::scalar(arguments, 5, value.ambient_intensity))
    {
        arguments.set_return(target.set_settings(value));
    }
}

void tricolor(const Arguments& arguments)
{
    auto target = environment(arguments, 12);
    if (!target)
    {
        return;
    }
    auto value = target.settings();
    value.ambient_tricolor = true;
    if (read_color(arguments, 2, value.ambient_sky_color) &&
        read_color(arguments, 5, value.ambient_equator_color) &&
        read_color(arguments, 8, value.ambient_ground_color) &&
        BindingContext::scalar(arguments, 11, value.ambient_intensity))
    {
        arguments.set_return(target.set_settings(value));
    }
}

void linear_fog(const Arguments& arguments)
{
    auto target = environment(arguments, 8);
    if (!target || !arguments.is_boolean(2))
    {
        return;
    }
    auto value = target.settings();
    value.fog_mode = arguments.boolean(2) ? schema::FogMode::linear : schema::FogMode::none;
    if (read_color(arguments, 3, value.fog_color) &&
        BindingContext::scalar(arguments, 6, value.fog_start) &&
        BindingContext::scalar(arguments, 7, value.fog_end))
    {
        arguments.set_return(target.set_settings(value));
    }
}

void exponential_fog(const Arguments& arguments)
{
    auto target = environment(arguments, 8);
    if (!target || !arguments.is_boolean(2) || !arguments.is_boolean(7))
    {
        return;
    }
    auto value = target.settings();
    value.fog_mode = !arguments.boolean(2)  ? schema::FogMode::none
                     : arguments.boolean(7) ? schema::FogMode::exponential_squared
                                            : schema::FogMode::exponential;
    if (read_color(arguments, 3, value.fog_color) &&
        BindingContext::scalar(arguments, 6, value.fog_density))
    {
        arguments.set_return(target.set_settings(value));
    }
}

void clear_color(const Arguments& arguments)
{
    auto target = environment(arguments, 3);
    if (!target || !arguments.is_uint32(2))
    {
        return;
    }
    auto value = target.settings();
    value.clear_color = arguments.unsigned_integer(2);
    arguments.set_return(target.set_settings(value));
}

void ibl(const Arguments& arguments)
{
    auto target = environment(arguments, 4);
    if (!target || !arguments.is_boolean(2))
    {
        return;
    }
    auto value = target.settings();
    value.ibl_enabled = arguments.boolean(2);
    if (BindingContext::scalar(arguments, 3, value.ibl_intensity))
    {
        arguments.set_return(target.set_settings(value));
    }
}

void atmosphere(const Arguments& arguments)
{
    auto target = environment(arguments, 6);
    auto* context = BindingContext::current(arguments);
    std::uint64_t sun_scene = 0, sun_id = 0;
    std::string text;
    if (!target || !context || !arguments.is_boolean(2) ||
        !BindingContext::identifier(arguments, 3, sun_scene) ||
        !BindingContext::identifier(arguments, 4, sun_id) ||
        !BindingContext::text(arguments, 5, text))
    {
        return;
    }
    auto settings = target.settings();
    settings.atmosphere_enabled = arguments.boolean(2);
    settings.atmosphere_sun_node_id = 0;
    if (sun_id != 0)
    {
        const auto sun = context->find_node(sun_scene, sun_id);
        const auto light = sun ? sun.get_component<Light>() : Light{};
        if (!sun || sun.scene() != target.node().scene() || !light ||
            light.settings().kind != schema::LightKind::directional)
        {
            return;
        }
        settings.atmosphere_sun_node_id = sun.info().properties->id;
    }
    const auto parameters = nlohmann::json::parse(text, nullptr, false);
    if (!parameters.is_object())
    {
        return;
    }
    const std::array scalars = {
        std::pair{"groundHeight", &settings.atmosphere_ground_height},
        std::pair{"rayleighScaleHeight", &settings.atmosphere_rayleigh_scale_height},
        std::pair{"mieScattering", &settings.atmosphere_mie_scattering},
        std::pair{"mieAbsorption", &settings.atmosphere_mie_absorption},
        std::pair{"mieScaleHeight", &settings.atmosphere_mie_scale_height},
        std::pair{"mieAnisotropy", &settings.atmosphere_mie_anisotropy},
        std::pair{"ozoneCenterHeight", &settings.atmosphere_ozone_center_height},
        std::pair{"ozoneHalfWidth", &settings.atmosphere_ozone_half_width},
        std::pair{"planetRadius", &settings.atmosphere_planet_radius},
        std::pair{"height", &settings.atmosphere_height},
        std::pair{"multiscatterStrength", &settings.atmosphere_multiscatter_strength}};
    const std::array vectors = {
        std::pair{"rayleighScattering", &settings.atmosphere_rayleigh_scattering},
        std::pair{"ozoneAbsorption", &settings.atmosphere_ozone_absorption},
        std::pair{"groundAlbedo", &settings.atmosphere_ground_albedo}};
    for (const auto& [key, value] : parameters.items())
    {
        bool recognized = false;
        for (const auto& [name, destination] : scalars)
        {
            if (key != name)
            {
                continue;
            }
            if (!value.is_number())
            {
                return;
            }
            const double number = value.get<double>();
            if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max())
            {
                return;
            }
            *destination = static_cast<float>(number);
            recognized = true;
            break;
        }
        for (const auto& [name, destination] : vectors)
        {
            if (key != name)
            {
                continue;
            }
            if (!value.is_array() || value.size() != 3)
            {
                return;
            }
            std::array<double, 3> channels{};
            for (std::size_t index = 0; index < channels.size(); ++index)
            {
                if (!value[index].is_number())
                {
                    return;
                }
                channels[index] = value[index].get<double>();
                if (!std::isfinite(channels[index]))
                {
                    return;
                }
            }
            *destination = Vec3(channels[0], channels[1], channels[2]);
            recognized = true;
            break;
        }
        if (!recognized)
        {
            return;
        }
    }
    arguments.set_return(target.set_settings(settings));
}

} // namespace

std::string_view EnvironmentComponentBinding::name() const noexcept
{
    return "EnvironmentComponent";
}

std::string_view EnvironmentComponentBinding::source() const noexcept
{
    return javascript;
}

bool EnvironmentComponentBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("EnvironmentKind", static_cast<int>(EnvironmentComponent::component_type)) &&
           vm.function("EnvironmentAdd", add) && vm.function("EnvironmentAmbient", ambient) &&
           vm.function("EnvironmentTricolor", tricolor) &&
           vm.function("EnvironmentLinearFog", linear_fog) &&
           vm.function("EnvironmentExponentialFog", exponential_fog) &&
           vm.function("EnvironmentClearColor", clear_color) &&
           vm.function("EnvironmentIbl", ibl) && vm.function("EnvironmentAtmosphere", atmosphere);
}
} // namespace engine::plugins::script::bindings
