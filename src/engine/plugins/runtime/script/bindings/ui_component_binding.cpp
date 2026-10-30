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
        handles.get(this).view = native.UiComponentId(scene, node);
    }
    IsValid() {
        const h = handles.get(this);
        return h !== undefined && native.UiComponentValid(h.scene, h.node, h.view);
    }
    SetEnabled(enabled) {
        const h = handles.get(this);
        return h !== undefined && native.UiComponentEnabled(h.scene, h.node, h.view, enabled);
    }
    SetDocument(path) {
        const h = handles.get(this);
        return h !== undefined && native.UiDocumentSet(h.scene, h.node, h.view, path);
    }
    FindElement(id) {
        const h = handles.get(this);
        if (h === undefined) return null;
        const found = native.UiElementFind(h.scene, h.node, h.view, id);
        return found === null ? null :
            new UiElement(key, h.scene, h.node, found.document, found.element);
    }
    SetVisible(visible) {
        const h = handles.get(this);
        return h !== undefined && native.UiDocumentVisibleSet(h.scene, h.node, h.view, visible);
    }
    get visible() {
        const h = handles.get(this);
        return h !== undefined && native.UiDocumentVisible(h.scene, h.node, h.view);
    }
}
function uiViewDocument(view) {
    const h = handles.get(view);
    return view instanceof UiComponent && h !== undefined ?
        native.UiDocumentId(h.scene, h.node, h.view) : 0n;
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
                   component->view_id() == id
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
    arguments.GetReturnValue().Set(arguments.Length() == 2 && node != nullptr &&
                                   node->add_component<UiComponent>() != nullptr);
}

void identifier(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    const auto* component = node == nullptr ? nullptr : node->get_component<UiComponent>();
    const auto id = arguments.Length() == 2 && component != nullptr ? component->view_id() : 0;
    arguments.GetReturnValue().Set(v8::BigInt::NewFromUnsigned(arguments.GetIsolate(), id));
}

void valid(const Arguments& arguments)
{
    arguments.GetReturnValue().Set(arguments.Length() == 3 && view(arguments) != nullptr);
}

void enabled(const Arguments& arguments)
{
    arguments.GetReturnValue().Set(false);
    auto* component = view(arguments);
    if (arguments.Length() == 4 && component != nullptr && arguments[3]->IsBoolean())
    {
        component->set_enabled(arguments[3]->IsTrue());
        arguments.GetReturnValue().Set(true);
    }
}

void set_document(const Arguments& arguments)
{
    arguments.GetReturnValue().Set(false);
    auto* component = view(arguments);
    std::string path;
    if (arguments.Length() == 4 && component != nullptr && BindingContext::text(arguments, 3, path))
    {
        arguments.GetReturnValue().Set(component->set_document(path));
    }
}

void find_element(const Arguments& arguments)
{
    arguments.GetReturnValue().SetNull();
    auto* component = view(arguments);
    auto* ui = system(arguments);
    std::string id;
    UiElementHandle handle;
    if (arguments.Length() != 4 || component == nullptr || ui == nullptr ||
        !BindingContext::text(arguments, 3, id) ||
        !ui->find_element(component->node().id(), id, handle))
    {
        return;
    }
    auto* isolate = arguments.GetIsolate();
    const auto context = isolate->GetCurrentContext();
    const auto result = v8::Object::New(isolate);
    if (BindingContext::define(context, result, "document",
                               v8::BigInt::NewFromUnsigned(isolate, handle.document_id)) &&
        BindingContext::define(context, result, "element",
                               v8::BigInt::NewFromUnsigned(isolate, handle.element_id)))
    {
        arguments.GetReturnValue().Set(result);
    }
}

void document_identifier(const Arguments& arguments)
{
    auto* component = view(arguments);
    auto* ui = system(arguments);
    const auto id = arguments.Length() == 3 && component != nullptr && ui != nullptr
                        ? ui->document_id(component->node().id())
                        : 0;
    arguments.GetReturnValue().Set(v8::BigInt::NewFromUnsigned(arguments.GetIsolate(), id));
}

void set_visible(const Arguments& arguments)
{
    auto* component = view(arguments);
    auto* ui = system(arguments);
    arguments.GetReturnValue().Set(
        arguments.Length() == 4 && component != nullptr && ui != nullptr &&
        arguments[3]->IsBoolean() &&
        ui->set_document_visible(component->node().id(), arguments[3]->IsTrue()));
}

void visible(const Arguments& arguments)
{
    auto* component = view(arguments);
    auto* ui = system(arguments);
    arguments.GetReturnValue().Set(arguments.Length() == 3 && component != nullptr &&
                                   ui != nullptr && ui->document_visible(component->node().id()));
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

bool UiComponentBinding::register_functions(v8::Local<v8::Context> context,
                                            v8::Local<v8::Object> native) const
{
    return BindingContext::integer(context, native, "UiComponentKind",
                                   static_cast<int>(UiComponent::component_type)) &&
           BindingContext::function(context, native, "UiComponentAdd", add) &&
           BindingContext::function(context, native, "UiComponentId", identifier) &&
           BindingContext::function(context, native, "UiComponentValid", valid) &&
           BindingContext::function(context, native, "UiComponentEnabled", enabled) &&
           BindingContext::function(context, native, "UiDocumentSet", set_document) &&
           BindingContext::function(context, native, "UiElementFind", find_element) &&
           BindingContext::function(context, native, "UiDocumentId", document_identifier) &&
           BindingContext::function(context, native, "UiDocumentVisibleSet", set_visible) &&
           BindingContext::function(context, native, "UiDocumentVisible", visible);
}

} // namespace engine::plugins::script::bindings
