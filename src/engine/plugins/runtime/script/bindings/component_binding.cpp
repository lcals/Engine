#include "plugins/runtime/script/bindings/component_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/component.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Component {
    constructor(token, scene, node, kind) { attach(this, token, scene, node, kind); }
    get node() { const h = handles.get(this); return new Node(key, h.scene, h.node); }
    IsValid() {
        const h = handles.get(this); return native.ComponentValid(h.scene, h.node, h.kind);
    }
    SetEnabled(enabled) {
        const h = handles.get(this); return native.ComponentEnabled(h.scene, h.node, h.kind, enabled);
    }
}
Object.freeze(Component.prototype);
Object.freeze(Component);
)js";

void valid(const Arguments& arguments)
{
    arguments.set_return(arguments.count() == 3 && BindingContext::component(arguments) != nullptr);
}

void enabled(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* component = BindingContext::component(arguments);
    if (component != nullptr && arguments.count() == 4 && arguments.is_boolean(3))
    {
        component->set_enabled(arguments.boolean(3));
        arguments.set_return(true);
    }
}
} // namespace

std::string_view ComponentBinding::name() const noexcept
{
    return "Component";
}

std::string_view ComponentBinding::source() const noexcept
{
    return javascript;
}

bool ComponentBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.function("ComponentValid", valid) && vm.function("ComponentEnabled", enabled);
}

} // namespace engine::plugins::script::bindings
