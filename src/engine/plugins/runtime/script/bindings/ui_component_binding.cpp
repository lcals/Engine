#include "plugins/runtime/script/bindings/ui_component_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/ui_component.hpp"
#include "runtime/ui/ui_system.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class UiComponent extends Component {
    constructor(token, scene, node) {
        super(token, scene, node, native.UiComponentKind);
    }
    SetDocument(path) {
        const h = handles.get(this);
        return h !== undefined && native.UiDocumentSet(h.scene, h.node, h.instance, path);
    }
    FindElement(id) {
        const h = handles.get(this);
        if (h === undefined) return null;
        const found = native.UiElementFind(h.scene, h.node, h.instance, id);
        return found === null ? null :
            new UiElement(key, h.scene, h.node, found.document, found.element);
    }
}
function uiViewDocument(view) {
    const h = handles.get(view);
    return view instanceof UiComponent && h !== undefined ?
        native.UiDocumentId(h.scene, h.node, h.instance) : 0n;
}
componentTypes.set(UiComponent, { add: native.UiComponentAdd, kind: native.UiComponentKind });
Object.freeze(UiComponent.prototype);
Object.freeze(UiComponent);
)js";

UiComponent* view(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    auto* component = node == nullptr ? nullptr : node->get_component<UiComponent>();
    std::uint64_t id = 0;
    return component != nullptr && BindingContext::identifier(arguments, 2, id) &&
                   component->instance_id() == id
               ? component
               : nullptr;
}

UiSystem* system(const Arguments& arguments)
{
    auto* context = BindingContext::current(arguments);
    auto* ui = context == nullptr ? nullptr : context->ui();
    return ui != nullptr && ui->is_initialized() ? ui : nullptr;
}

void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node != nullptr &&
                         node->add_component<UiComponent>() != nullptr);
}

void set_document(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* component = view(arguments);
    std::string path;
    if (arguments.count() == 4 && component != nullptr && BindingContext::text(arguments, 3, path))
    {
        arguments.set_return(component->set_document(path));
    }
}

void find_element(const Arguments& arguments)
{
    arguments.set_return({});
    auto* component = view(arguments);
    auto* ui = system(arguments);
    std::string id;
    UiElementHandle handle;
    if (arguments.count() != 4 || component == nullptr || ui == nullptr ||
        !BindingContext::text(arguments, 3, id) ||
        !ui->find_element(component->node().id(), id, handle))
    {
        return;
    }
    arguments.set_return(
        vm::NativeValue::Object{{"document", handle.document_id}, {"element", handle.element_id}});
}

void document_identifier(const Arguments& arguments)
{
    auto* component = view(arguments);
    auto* ui = system(arguments);
    const auto id = arguments.count() == 3 && component != nullptr && ui != nullptr
                        ? ui->document_id(component->node().id())
                        : 0;
    arguments.set_return(id);
}

} // namespace

std::string_view UiComponentBinding::name() const noexcept
{
    return "UiComponent";
}

std::string_view UiComponentBinding::source() const noexcept
{
    return javascript;
}

bool UiComponentBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("UiComponentKind", static_cast<int>(UiComponent::component_type)) &&
           vm.function("UiComponentAdd", add) && vm.function("UiDocumentSet", set_document) &&
           vm.function("UiElementFind", find_element) &&
           vm.function("UiDocumentId", document_identifier);
}

} // namespace engine::plugins::script::bindings
