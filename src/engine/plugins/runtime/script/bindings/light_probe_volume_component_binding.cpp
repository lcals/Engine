#include "plugins/runtime/script/bindings/light_probe_volume_component_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/light_probe_volume_component.hpp"
#include "runtime/scene/node.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class LightProbeVolumeComponent extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.LightProbeVolumeKind); }
    SetResource(path) {
        const h = handles.get(this); return native.LightProbeVolumeResource(h.scene, h.node, path);
    }
}
componentTypes.set(LightProbeVolumeComponent,
    { add: native.LightProbeVolumeAdd, kind: native.LightProbeVolumeKind });
Object.freeze(LightProbeVolumeComponent.prototype);
Object.freeze(LightProbeVolumeComponent);
)js";
void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node &&
                         node->add_component<LightProbeVolumeComponent>() != nullptr);
}
void resource(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    auto* component = node ? node->get_component<LightProbeVolumeComponent>() : nullptr;
    std::string path;
    LightProbeVolumeSettings settings;
    if (component && arguments.count() == 3 && BindingContext::text(arguments, 2, path) &&
        (path.empty() || ResourceSystem::path_to_guid(path, settings.probes_guid)))
    {
        arguments.set_return(component->set_settings(settings));
    }
}
} // namespace
std::string_view LightProbeVolumeComponentBinding::name() const noexcept
{
    return "LightProbeVolumeComponent";
}
std::string_view LightProbeVolumeComponentBinding::source() const noexcept
{
    return javascript;
}
bool LightProbeVolumeComponentBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("LightProbeVolumeKind",
                      static_cast<int>(LightProbeVolumeComponent::component_type)) &&
           vm.function("LightProbeVolumeAdd", add) &&
           vm.function("LightProbeVolumeResource", resource);
}
} // namespace engine::plugins::script::bindings
