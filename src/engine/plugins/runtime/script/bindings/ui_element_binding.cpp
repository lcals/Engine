#include "plugins/runtime/script/bindings/ui_element_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/ui/ui_system.hpp"
#include "runtime/world/world_system.hpp"

#include <limits>
#include <utility>

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
    if (arguments.count() < 4 || node == nullptr || ui == nullptr || !ui->is_initialized() ||
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
    arguments.set_return(arguments.count() == 4 && element(arguments, handle) != nullptr);
}

void get_string(const Arguments& arguments, bool (UiSystem::*read)(UiElementHandle, std::string&))
{
    arguments.set_return({});
    UiElementHandle handle;
    auto* ui = element(arguments, handle);
    std::string value;
    if (arguments.count() != 4 || ui == nullptr || !(ui->*read)(handle, value) ||
        value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return;
    }
    arguments.set_return(value);
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
    arguments.set_return(false);
    UiElementHandle handle;
    auto* ui = element(arguments, handle);
    std::string value;
    if (arguments.count() == 5 && ui != nullptr && BindingContext::text(arguments, 4, value))
    {
        arguments.set_return((ui->*write)(handle, value));
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
    arguments.set_return(arguments.count() == 5 && ui != nullptr && arguments.is_boolean(4) &&
                         (ui->*write)(handle, arguments.boolean(4)));
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
    arguments.set_return(arguments.count() == 6 && ui != nullptr &&
                         BindingContext::text(arguments, 4, name) && arguments.is_boolean(5) &&
                         ui->set_class(handle, name, arguments.boolean(5)));
}

void set_style(const Arguments& arguments)
{
    UiElementHandle handle;
    auto* ui = element(arguments, handle);
    std::string name, value;
    arguments.set_return(
        arguments.count() == 6 && ui != nullptr && BindingContext::text(arguments, 4, name) &&
        BindingContext::text(arguments, 5, value) && ui->set_style(handle, name, value));
}

void poll_event(const Arguments& arguments)
{
    arguments.set_return({});
    auto* binding = BindingContext::current(arguments);
    auto* ui = binding == nullptr ? nullptr : binding->ui();
    auto* world = binding == nullptr ? nullptr : binding->world();
    const auto* scene = world == nullptr ? nullptr : world->scene();
    if (arguments.count() != 0 || ui == nullptr || !ui->is_initialized() || scene == nullptr ||
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
        if (event.path.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return;
        }
        vm::NativeValue::Array route;
        route.reserve(event.path.size());
        for (const auto& handle : event.path)
        {
            if (handle.document_id != event.target.document_id || handle.element_id == 0)
            {
                return;
            }
            route.emplace_back(vm::NativeValue::Object{{"document", handle.document_id},
                                                       {"element", handle.element_id}});
        }
        arguments.set_return(vm::NativeValue::Object{{"scene", event.scene_id},
                                                     {"node", event.node_id},
                                                     {"document", event.target.document_id},
                                                     {"element", event.target.element_id},
                                                     {"type", event.type},
                                                     {"id", event.element_id},
                                                     {"value", event.value},
                                                     {"route", std::move(route)}});
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

bool UiElementBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.function("UiElementValid", valid) && vm.function("UiElementId", identifier) &&
           vm.function("UiElementText", text) && vm.function("UiElementValue", value) &&
           vm.function("UiElementTextSet", set_text) &&
           vm.function("UiElementValueSet", set_value) &&
           vm.function("UiElementVisibleSet", set_visible) &&
           vm.function("UiElementEnabledSet", set_enabled) &&
           vm.function("UiElementClassSet", set_class) &&
           vm.function("UiElementStyleSet", set_style) &&
           vm.function("UiElementImageSet", set_image) && vm.function("UiPollEvent", poll_event);
}

} // namespace engine::plugins::script::bindings
