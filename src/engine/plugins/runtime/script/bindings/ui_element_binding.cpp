#include "plugins/runtime/script/bindings/ui_element_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/ui/ui_system.hpp"
#include "runtime/world/world_system.hpp"

#include <limits>

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
const uiSubscriptions = new Map();
let uiSubscriptionSequence = 0;
function uiElementArguments(element) {
    const h = handles.get(element);
    return element instanceof UiElement && h !== undefined ?
        [h.scene, h.node, h.document, h.element] : null;
}
function uiSameElement(left, right) {
    const a = handles.get(left), b = handles.get(right);
    return a !== undefined && b !== undefined && a.scene === b.scene && a.node === b.node &&
        a.document === b.document && a.element === b.element;
}
export class UiElement {
    constructor(token, scene, node, document, element) {
        attach(this, token, scene, node);
        const h = handles.get(this);
        h.document = document;
        h.element = element;
    }
    get isValid() {
        const args = uiElementArguments(this);
        return args !== null && native.UiElementValid(...args);
    }
    get id() {
        const args = uiElementArguments(this);
        return args === null ? null : native.UiElementId(...args);
    }
    get text() {
        const args = uiElementArguments(this);
        return args === null ? null : native.UiElementText(...args);
    }
    get value() {
        const args = uiElementArguments(this);
        return args === null ? null : native.UiElementValue(...args);
    }
    SetText(value) {
        const args = uiElementArguments(this);
        return args !== null && native.UiElementTextSet(...args, value);
    }
    SetValue(value) {
        const args = uiElementArguments(this);
        return args !== null && native.UiElementValueSet(...args, value);
    }
    SetVisible(value) {
        const args = uiElementArguments(this);
        return args !== null && native.UiElementVisibleSet(...args, value);
    }
    SetEnabled(value) {
        const args = uiElementArguments(this);
        return args !== null && native.UiElementEnabledSet(...args, value);
    }
    SetClass(name, active) {
        const args = uiElementArguments(this);
        return args !== null && native.UiElementClassSet(...args, name, active);
    }
    SetStyle(name, value) {
        const args = uiElementArguments(this);
        return args !== null && native.UiElementStyleSet(...args, name, value);
    }
    SetImage(path) {
        const args = uiElementArguments(this);
        return args !== null && native.UiElementImageSet(...args, path);
    }
    On(type, callback) {
        if ((type !== "click" && type !== "change" && type !== "input") ||
            typeof callback !== "function" || !this.isValid ||
            uiSubscriptionSequence === Number.MAX_SAFE_INTEGER) return 0;
        const subscription = ++uiSubscriptionSequence;
        uiSubscriptions.set(subscription, { element: this, type, callback });
        return subscription;
    }
    Off(subscription) {
        const current = uiSubscriptions.get(subscription);
        return current !== undefined && uiSameElement(current.element, this) &&
            uiSubscriptions.delete(subscription);
    }
}
Object.freeze(UiElement.prototype);
Object.freeze(UiElement);
)js";

UiSystem* element(const Arguments& arguments, UiElementHandle& handle)
{
    handle = {};
    auto* node = BindingContext::node(arguments);
    auto* binding = BindingContext::current(arguments);
    auto* ui = binding == nullptr ? nullptr : binding->ui();
    if (arguments.Length() < 4 || node == nullptr || ui == nullptr || !ui->is_initialized() ||
        !BindingContext::identifier(arguments, 2, handle.document_id) ||
        !BindingContext::identifier(arguments, 3, handle.element_id) || handle.document_id == 0 ||
        handle.element_id == 0 || ui->document_id(node->id()) != handle.document_id ||
        !ui->is_valid(handle))
    {
        handle = {};
        return nullptr;
    }
    return ui;
}

void valid(const Arguments& arguments)
{
    UiElementHandle handle;
    arguments.GetReturnValue().Set(arguments.Length() == 4 &&
                                   element(arguments, handle) != nullptr);
}

void get_string(const Arguments& arguments, bool (UiSystem::*read)(UiElementHandle, std::string&))
{
    arguments.GetReturnValue().SetNull();
    UiElementHandle handle;
    auto* ui = element(arguments, handle);
    std::string value;
    if (arguments.Length() != 4 || ui == nullptr || !(ui->*read)(handle, value) ||
        value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return;
    }
    v8::Local<v8::String> result;
    if (v8::String::NewFromUtf8(arguments.GetIsolate(), value.data(), v8::NewStringType::kNormal,
                                static_cast<int>(value.size()))
            .ToLocal(&result))
    {
        arguments.GetReturnValue().Set(result);
    }
}

void identifier(const Arguments& arguments)
{
    get_string(arguments, &UiSystem::element_id);
}

void text(const Arguments& arguments)
{
    get_string(arguments, &UiSystem::text);
}

void value(const Arguments& arguments)
{
    get_string(arguments, &UiSystem::value);
}

void set_string(const Arguments& arguments,
                bool (UiSystem::*write)(UiElementHandle, std::string_view))
{
    arguments.GetReturnValue().Set(false);
    UiElementHandle handle;
    auto* ui = element(arguments, handle);
    std::string value;
    if (arguments.Length() == 5 && ui != nullptr && BindingContext::text(arguments, 4, value))
    {
        arguments.GetReturnValue().Set((ui->*write)(handle, value));
    }
}

void set_text(const Arguments& arguments)
{
    set_string(arguments, &UiSystem::set_text);
}

void set_value(const Arguments& arguments)
{
    set_string(arguments, &UiSystem::set_value);
}

void set_image(const Arguments& arguments)
{
    set_string(arguments, &UiSystem::set_image);
}

void set_boolean(const Arguments& arguments, bool (UiSystem::*write)(UiElementHandle, bool))
{
    UiElementHandle handle;
    auto* ui = element(arguments, handle);
    arguments.GetReturnValue().Set(arguments.Length() == 5 && ui != nullptr &&
                                   arguments[4]->IsBoolean() &&
                                   (ui->*write)(handle, arguments[4]->IsTrue()));
}

void set_visible(const Arguments& arguments)
{
    set_boolean(arguments, &UiSystem::set_visible);
}

void set_enabled(const Arguments& arguments)
{
    set_boolean(arguments, &UiSystem::set_enabled);
}

void set_class(const Arguments& arguments)
{
    UiElementHandle handle;
    auto* ui = element(arguments, handle);
    std::string name;
    arguments.GetReturnValue().Set(
        arguments.Length() == 6 && ui != nullptr && BindingContext::text(arguments, 4, name) &&
        arguments[5]->IsBoolean() && ui->set_class(handle, name, arguments[5]->IsTrue()));
}

void set_style(const Arguments& arguments)
{
    UiElementHandle handle;
    auto* ui = element(arguments, handle);
    std::string name, value;
    arguments.GetReturnValue().Set(
        arguments.Length() == 6 && ui != nullptr && BindingContext::text(arguments, 4, name) &&
        BindingContext::text(arguments, 5, value) && ui->set_style(handle, name, value));
}

void poll_event(const Arguments& arguments)
{
    arguments.GetReturnValue().SetNull();
    auto* binding = BindingContext::current(arguments);
    auto* ui = binding == nullptr ? nullptr : binding->ui();
    auto* world = binding == nullptr ? nullptr : binding->world();
    const auto* scene = world == nullptr ? nullptr : world->scene();
    if (arguments.Length() != 0 || ui == nullptr || !ui->is_initialized() || scene == nullptr ||
        binding->scene_id() != scene->id())
    {
        return;
    }
    UiEvent event;
    while (ui->poll_event(event))
    {
        if (event.scene_id != binding->scene_id() ||
            ui->document_id(event.node_id) != event.target.document_id ||
            !ui->is_valid(event.target))
        {
            continue;
        }
        auto* isolate = arguments.GetIsolate();
        const auto context = isolate->GetCurrentContext();
        const auto result = v8::Object::New(isolate);
        if (event.path.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return;
        }
        const auto route = v8::Array::New(isolate, static_cast<int>(event.path.size()));
        std::uint32_t index = 0;
        for (const auto& handle : event.path)
        {
            const auto ancestor = v8::Object::New(isolate);
            if (handle.document_id != event.target.document_id || handle.element_id == 0 ||
                !BindingContext::define(context, ancestor, "document",
                                        v8::BigInt::NewFromUnsigned(isolate, handle.document_id)) ||
                !BindingContext::define(context, ancestor, "element",
                                        v8::BigInt::NewFromUnsigned(isolate, handle.element_id)) ||
                !route->Set(context, index++, ancestor).FromMaybe(false))
            {
                return;
            }
        }
        if (BindingContext::define(context, result, "scene",
                                   v8::BigInt::NewFromUnsigned(isolate, event.scene_id)) &&
            BindingContext::define(context, result, "node",
                                   v8::BigInt::NewFromUnsigned(isolate, event.node_id)) &&
            BindingContext::define(
                context, result, "document",
                v8::BigInt::NewFromUnsigned(isolate, event.target.document_id)) &&
            BindingContext::define(context, result, "element",
                                   v8::BigInt::NewFromUnsigned(isolate, event.target.element_id)) &&
            BindingContext::string(context, result, "type", event.type) &&
            BindingContext::string(context, result, "id", event.element_id) &&
            BindingContext::string(context, result, "value", event.value) &&
            BindingContext::define(context, result, "route", route))
        {
            arguments.GetReturnValue().Set(result);
        }
        return;
    }
}
} // namespace

std::string_view UiElementBinding::name() const noexcept
{
    return "UiElement";
}

std::string_view UiElementBinding::source() const noexcept
{
    return javascript;
}

bool UiElementBinding::register_functions(v8::Local<v8::Context> context,
                                          v8::Local<v8::Object> native) const
{
    return BindingContext::function(context, native, "UiElementValid", valid) &&
           BindingContext::function(context, native, "UiElementId", identifier) &&
           BindingContext::function(context, native, "UiElementText", text) &&
           BindingContext::function(context, native, "UiElementValue", value) &&
           BindingContext::function(context, native, "UiElementTextSet", set_text) &&
           BindingContext::function(context, native, "UiElementValueSet", set_value) &&
           BindingContext::function(context, native, "UiElementVisibleSet", set_visible) &&
           BindingContext::function(context, native, "UiElementEnabledSet", set_enabled) &&
           BindingContext::function(context, native, "UiElementClassSet", set_class) &&
           BindingContext::function(context, native, "UiElementStyleSet", set_style) &&
           BindingContext::function(context, native, "UiElementImageSet", set_image) &&
           BindingContext::function(context, native, "UiPollEvent", poll_event);
}

} // namespace engine::plugins::script::bindings
