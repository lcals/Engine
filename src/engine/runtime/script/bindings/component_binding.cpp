#include "runtime/script/bindings/component_binding.hpp"

#include "runtime/scene/component.hpp"
#include "runtime/script/bindings/binding_context.hpp"

namespace engine::script_bindings
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
    arguments.GetReturnValue().Set(arguments.Length() == 3 &&
                                   BindingContext::component(arguments) != nullptr);
}

void enabled(const Arguments& arguments)
{
    arguments.GetReturnValue().Set(false);
    auto* component = BindingContext::component(arguments);
    if (component != nullptr && arguments.Length() == 4 && arguments[3]->IsBoolean())
    {
        component->set_enabled(arguments[3]->IsTrue());
        arguments.GetReturnValue().Set(true);
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

bool ComponentBinding::register_functions(v8::Local<v8::Context> context,
                                          v8::Local<v8::Object> native) const
{
    return BindingContext::function(context, native, "ComponentValid", valid) &&
           BindingContext::function(context, native, "ComponentEnabled", enabled);
}

} // namespace engine::script_bindings
