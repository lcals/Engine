#include "plugins/runtime/script/bindings/planar_reflection_component_binding.hpp"
#include "plugins/runtime/script/bindings/binding_helpers.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/planar_reflection_component.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class PlanarReflectionComponent extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.PlanarReflectionKind); }
    SetSettings(resolutionScale = 0.5, clipBias = 0.02) {
        const h = handles.get(this);
        return native.PlanarReflectionSettings(h.scene, h.node, resolutionScale, clipBias);
    }
}
componentTypes.set(PlanarReflectionComponent,
    { add: native.PlanarReflectionAdd, kind: native.PlanarReflectionKind });
Object.freeze(PlanarReflectionComponent.prototype);
Object.freeze(PlanarReflectionComponent);
)js";

void settings(const Arguments& arguments)
{
    arguments.set_return(false);
    auto node = BindingContext::node(arguments);
    auto component =
        node ? node.get_component<PlanarReflectionComponent>() : PlanarReflectionComponent{};
    PlanarReflectionSettings settings;
    if (component && arguments.count() == 4 &&
        BindingContext::scalar(arguments, 2, settings.resolution_scale) &&
        BindingContext::scalar(arguments, 3, settings.clip_bias))
    {
        arguments.set_return(component.set_settings(settings));
    }
}
} // namespace

std::string_view PlanarReflectionComponentBinding::name() const noexcept
{
    return "PlanarReflectionComponent";
}
std::string_view PlanarReflectionComponentBinding::source() const noexcept
{
    return javascript;
}
bool PlanarReflectionComponentBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("PlanarReflectionKind",
                      static_cast<int>(PlanarReflectionComponent::component_type)) &&
           vm.function("PlanarReflectionAdd",
                       binding_detail::add_component<PlanarReflectionComponent>) &&
           vm.function("PlanarReflectionSettings", settings);
}
} // namespace engine::plugins::script::bindings
