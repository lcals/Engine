#include "plugins/runtime/script/bindings/component_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/component.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
function copyComponentData(value) {
    if (Array.isArray(value)) return value.map(copyComponentData);
    if (value === null || typeof value !== 'object') return value;
    const result = {};
    for (const [key, item] of Object.entries(value)) result[key] = copyComponentData(item);
    return result;
}
export class Component {
    constructor(token, scene, node, kind) { attach(this, token, scene, node, kind); }
    get node() { const h = handles.get(this); return new Node(key, h.scene, h.node); }
    IsValid() {
        const h = handles.get(this);
        return native.ComponentValid(h.scene, h.node, h.kind);
    }
    get enabled() {
        const h = handles.get(this); return native.ComponentLocalEnabled(h.scene, h.node, h.kind);
    }
    get active() {
        const h = handles.get(this); return native.ComponentActive(h.scene, h.node, h.kind);
    }
    get visible() {
        const h = handles.get(this); return native.ComponentVisible(h.scene, h.node, h.kind);
    }
    get visibleInHierarchy() {
        const h = handles.get(this); return native.ComponentVisibleInHierarchy(h.scene, h.node, h.kind);
    }
    SetVisible(visible) {
        const h = handles.get(this); return native.ComponentVisibleSet(h.scene, h.node, h.kind, visible);
    }
    SetEnabled(enabled) {
        const h = handles.get(this);
        return native.ComponentEnabled(h.scene, h.node, h.kind, enabled);
    }
}
Object.freeze(Component.prototype);
Object.freeze(Component);
)js";

void valid(const Arguments& arguments)
{
    arguments.set_return(arguments.count() == 3 && BindingContext::component(arguments).valid());
}

void identity(const Arguments& arguments)
{
    const auto component = BindingContext::component(arguments);
    arguments.set_return(arguments.count() == 3 && component.valid() ? component.instance_id()
                                                                     : std::uint64_t{0});
}

void enabled(const Arguments& arguments)
{
    arguments.set_return(false);
    auto component = BindingContext::component(arguments);
    if (component.valid() && arguments.count() == 4 && arguments.is_boolean(3))
    {
        arguments.set_return(component.set_enabled(arguments.boolean(3)));
    }
}

void state(const Arguments& arguments)
{
    arguments.set_return({});
    const auto target = BindingContext::component(arguments);
    if (!target || arguments.count() != 3)
    {
        return;
    }
    switch (arguments.data())
    {
    case 0:
        arguments.set_return(target.enabled());
        break;
    case 1:
        arguments.set_return(target.active());
        break;
    case 2:
        arguments.set_return(target.visible());
        break;
    case 3:
        arguments.set_return(target.visible_in_hierarchy());
        break;
    }
}

void set_visible(const Arguments& arguments)
{
    arguments.set_return(false);
    auto target = BindingContext::component(arguments);
    if (target.valid() && arguments.count() == 4 && arguments.is_boolean(3))
    {
        arguments.set_return(target.set_visible(arguments.boolean(3)));
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
    return vm.function("ComponentValid", valid) && vm.function("ComponentIdentity", identity) &&
           vm.function("ComponentEnabled", enabled) &&
           vm.function("ComponentLocalEnabled", state, 0) &&
           vm.function("ComponentActive", state, 1) && vm.function("ComponentVisible", state, 2) &&
           vm.function("ComponentVisibleInHierarchy", state, 3) &&
           vm.function("ComponentVisibleSet", set_visible);
}

} // namespace engine::plugins::script::bindings
