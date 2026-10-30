#include "plugins/runtime/script/bindings/environment_component_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/node.hpp"

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
    SetIbl(enabled, intensity = 1) {
        const h = handles.get(this); return native.EnvironmentIbl(h.scene, h.node, enabled, intensity);
    }
}
componentTypes.set(EnvironmentComponent, { add: native.EnvironmentAdd, kind: native.EnvironmentKind });
Object.freeze(EnvironmentComponent.prototype);
Object.freeze(EnvironmentComponent);
)js";

EnvironmentComponent* environment(const Arguments& arguments, int count)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    return node != nullptr && arguments.count() == count
               ? node->get_component<EnvironmentComponent>()
               : nullptr;
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
    auto* node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node != nullptr &&
                         node->add_component<EnvironmentComponent>() != nullptr);
}

void ambient(const Arguments& arguments)
{
    auto* target = environment(arguments, 6);
    if (target == nullptr)
    {
        return;
    }
    auto value = target->settings();
    value.ambient_tricolor = false;
    if (read_color(arguments, 2, value.ambient_color) &&
        BindingContext::scalar(arguments, 5, value.ambient_intensity))
    {
        arguments.set_return(target->set_settings(value));
    }
}

void tricolor(const Arguments& arguments)
{
    auto* target = environment(arguments, 12);
    if (target == nullptr)
    {
        return;
    }
    auto value = target->settings();
    value.ambient_tricolor = true;
    if (read_color(arguments, 2, value.ambient_sky_color) &&
        read_color(arguments, 5, value.ambient_equator_color) &&
        read_color(arguments, 8, value.ambient_ground_color) &&
        BindingContext::scalar(arguments, 11, value.ambient_intensity))
    {
        arguments.set_return(target->set_settings(value));
    }
}

void linear_fog(const Arguments& arguments)
{
    auto* target = environment(arguments, 8);
    if (target == nullptr || !arguments.is_boolean(2))
    {
        return;
    }
    auto value = target->settings();
    value.fog_mode = arguments.boolean(2) ? schema::FogMode::linear : schema::FogMode::none;
    if (read_color(arguments, 3, value.fog_color) &&
        BindingContext::scalar(arguments, 6, value.fog_start) &&
        BindingContext::scalar(arguments, 7, value.fog_end))
    {
        arguments.set_return(target->set_settings(value));
    }
}

void exponential_fog(const Arguments& arguments)
{
    auto* target = environment(arguments, 8);
    if (target == nullptr || !arguments.is_boolean(2) || !arguments.is_boolean(7))
    {
        return;
    }
    auto value = target->settings();
    value.fog_mode = !arguments.boolean(2)  ? schema::FogMode::none
                     : arguments.boolean(7) ? schema::FogMode::exponential_squared
                                            : schema::FogMode::exponential;
    if (read_color(arguments, 3, value.fog_color) &&
        BindingContext::scalar(arguments, 6, value.fog_density))
    {
        arguments.set_return(target->set_settings(value));
    }
}

void clear_color(const Arguments& arguments)
{
    auto* target = environment(arguments, 3);
    if (target == nullptr || !arguments.is_uint32(2))
    {
        return;
    }
    auto value = target->settings();
    value.clear_color = arguments.unsigned_integer(2);
    arguments.set_return(target->set_settings(value));
}

void ibl(const Arguments& arguments)
{
    auto* target = environment(arguments, 4);
    if (target == nullptr || !arguments.is_boolean(2))
    {
        return;
    }
    auto value = target->settings();
    value.ibl_enabled = arguments.boolean(2);
    if (BindingContext::scalar(arguments, 3, value.ibl_intensity))
    {
        arguments.set_return(target->set_settings(value));
    }
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
           vm.function("EnvironmentClearColor", clear_color) && vm.function("EnvironmentIbl", ibl);
}
} // namespace engine::plugins::script::bindings
