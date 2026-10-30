#include "plugins/runtime/script/bindings/post_process_component_binding.hpp"

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
}
componentTypes.set(PostProcessComponent, { add: native.PostProcessAdd, kind: native.PostProcessKind });
Object.freeze(PostProcessComponent.prototype);
Object.freeze(PostProcessComponent);
)js";

void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node != nullptr &&
                         node->add_component<PostProcessComponent>() != nullptr);
}

void exposure(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    auto* target = node == nullptr ? nullptr : node->get_component<PostProcessComponent>();
    if (target == nullptr || arguments.count() != 3)
    {
        return;
    }
    auto value = target->settings();
    if (BindingContext::scalar(arguments, 2, value.exposure))
    {
        arguments.set_return(target->set_settings(value));
    }
}

void filmic_tonemap(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    auto* target = node == nullptr ? nullptr : node->get_component<PostProcessComponent>();
    if (target == nullptr || arguments.count() != 3 || !arguments.is_boolean(2))
    {
        return;
    }
    auto value = target->settings();
    value.filmic_tonemap = arguments.boolean(2);
    arguments.set_return(target->set_settings(value));
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
           vm.function("PostProcessAdd", add) && vm.function("PostProcessExposure", exposure) &&
           vm.function("PostProcessFilmicTonemap", filmic_tonemap);
}
} // namespace engine::plugins::script::bindings
