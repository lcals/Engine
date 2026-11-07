#include "plugins/runtime/script/bindings/post_process_component_binding.hpp"
#include "plugins/runtime/script/bindings/binding_helpers.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/post_process_component.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class PostProcessComponent extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.PostProcessKind); }
    SetExposure(value) {
        const h = handles.get(this); return native.PostProcessExposure(h.scene, h.node, value);
    }
    SetFilmicTonemap(enabled) {
        const h = handles.get(this); return native.PostProcessFilmicTonemap(h.scene, h.node, enabled);
    }
    SetHdr(enabled) {
        const h = handles.get(this); return native.PostProcessHdr(h.scene, h.node, enabled);
    }
    SetBloom(enabled, threshold = 1, intensity = 0.15, radius = 1) {
        const h = handles.get(this);
        return native.PostProcessBloom(h.scene, h.node, enabled, threshold, intensity, radius);
    }
    SetScreenSpaceReflections(enabled, resolutionScale = 0.5, maxSteps = 64,
                              maxDistance = 80, thickness = 0.15, roughnessCutoff = 0.7) {
        const h = handles.get(this);
        return native.PostProcessScreenSpaceReflections(
            h.scene, h.node, enabled, resolutionScale, maxSteps, maxDistance, thickness,
            roughnessCutoff);
    }
    SetAmbientOcclusion(enabled, radius = 1, strength = 1, power = 1.5,
                        fadeStart = 80, fadeEnd = 150) {
        const h = handles.get(this);
        return native.PostProcessAmbientOcclusion(
            h.scene, h.node, enabled, radius, strength, power, fadeStart, fadeEnd);
    }
}
componentTypes.set(PostProcessComponent, { add: native.PostProcessAdd, kind: native.PostProcessKind });
Object.freeze(PostProcessComponent.prototype);
Object.freeze(PostProcessComponent);
)js";

void exposure(const Arguments& arguments)
{
    arguments.set_return(false);
    auto node = BindingContext::node(arguments);
    auto target = !node ? PostProcessComponent{} : node.get_component<PostProcessComponent>();
    if (!target || arguments.count() != 3)
    {
        return;
    }
    PostProcessComponent::Data value{};
    if (!target.get_settings(value))
    {
        arguments.set_return(false);
        return;
    }
    if (BindingContext::scalar(arguments, 2, value.exposure))
    {
        arguments.set_return(target.set_settings(value));
    }
}

void filmic_tonemap(const Arguments& arguments)
{
    arguments.set_return(false);
    auto node = BindingContext::node(arguments);
    auto target = !node ? PostProcessComponent{} : node.get_component<PostProcessComponent>();
    if (!target || arguments.count() != 3 || !arguments.is_boolean(2))
    {
        return;
    }
    PostProcessComponent::Data value{};
    if (!target.get_settings(value))
    {
        arguments.set_return(false);
        return;
    }
    value.filmic_tonemap = arguments.boolean(2);
    arguments.set_return(target.set_settings(value));
}

void hdr(const Arguments& arguments)
{
    arguments.set_return(false);
    auto node = BindingContext::node(arguments);
    auto target = !node ? PostProcessComponent{} : node.get_component<PostProcessComponent>();
    if (!target || arguments.count() != 3 || !arguments.is_boolean(2))
    {
        return;
    }
    PostProcessComponent::Data settings{};
    if (!target.get_settings(settings))
    {
        arguments.set_return(false);
        return;
    }
    settings.hdr_enabled = arguments.boolean(2);
    arguments.set_return(target.set_settings(settings));
}

void bloom(const Arguments& arguments)
{
    arguments.set_return(false);
    auto node = BindingContext::node(arguments);
    auto target = !node ? PostProcessComponent{} : node.get_component<PostProcessComponent>();
    if (!target || arguments.count() != 6 || !arguments.is_boolean(2))
    {
        return;
    }
    PostProcessComponent::Data settings{};
    if (!target.get_settings(settings))
    {
        arguments.set_return(false);
        return;
    }
    settings.bloom_enabled = arguments.boolean(2);
    if (BindingContext::scalar(arguments, 3, settings.bloom_threshold) &&
        BindingContext::scalar(arguments, 4, settings.bloom_intensity) &&
        BindingContext::scalar(arguments, 5, settings.bloom_radius))
    {
        arguments.set_return(target.set_settings(settings));
    }
}

void screen_space_reflections(const Arguments& arguments)
{
    arguments.set_return(false);
    auto node = BindingContext::node(arguments);
    auto target = !node ? PostProcessComponent{} : node.get_component<PostProcessComponent>();
    if (!target || arguments.count() != 8 || !arguments.is_boolean(2) || !arguments.is_uint32(4) ||
        arguments.unsigned_integer(4) > 256)
    {
        return;
    }
    PostProcessComponent::Data settings{};
    if (!target.get_settings(settings))
    {
        arguments.set_return(false);
        return;
    }
    settings.ssr_enabled = arguments.boolean(2);
    settings.ssr_max_steps = static_cast<std::uint16_t>(arguments.unsigned_integer(4));
    if (BindingContext::scalar(arguments, 3, settings.ssr_resolution_scale) &&
        BindingContext::scalar(arguments, 5, settings.ssr_max_distance) &&
        BindingContext::scalar(arguments, 6, settings.ssr_thickness) &&
        BindingContext::scalar(arguments, 7, settings.ssr_roughness_cutoff))
    {
        arguments.set_return(target.set_settings(settings));
    }
}

void ambient_occlusion(const Arguments& arguments)
{
    arguments.set_return(false);
    auto node = BindingContext::node(arguments);
    auto target = !node ? PostProcessComponent{} : node.get_component<PostProcessComponent>();
    if (!target || arguments.count() != 8 || !arguments.is_boolean(2))
    {
        return;
    }
    PostProcessComponent::Data settings{};
    if (!target.get_settings(settings))
    {
        arguments.set_return(false);
        return;
    }
    settings.ao_enabled = arguments.boolean(2);
    if (BindingContext::scalar(arguments, 3, settings.ao_radius) &&
        BindingContext::scalar(arguments, 4, settings.ao_strength) &&
        BindingContext::scalar(arguments, 5, settings.ao_power) &&
        BindingContext::scalar(arguments, 6, settings.ao_fade_start) &&
        BindingContext::scalar(arguments, 7, settings.ao_fade_end))
    {
        arguments.set_return(target.set_settings(settings));
    }
}
} // namespace

std::string_view PostProcessComponentBinding::name() const noexcept
{
    return "PostProcessComponent";
}

std::string_view PostProcessComponentBinding::source() const noexcept
{
    return javascript;
}

bool PostProcessComponentBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("PostProcessKind", static_cast<int>(PostProcessComponent::component_type)) &&
           vm.function("PostProcessAdd", binding_detail::add_component<PostProcessComponent>) &&
           vm.function("PostProcessExposure", exposure) &&
           vm.function("PostProcessFilmicTonemap", filmic_tonemap) &&
           vm.function("PostProcessHdr", hdr) && vm.function("PostProcessBloom", bloom) &&
           vm.function("PostProcessAmbientOcclusion", ambient_occlusion) &&
           vm.function("PostProcessScreenSpaceReflections", screen_space_reflections);
}
} // namespace engine::plugins::script::bindings
