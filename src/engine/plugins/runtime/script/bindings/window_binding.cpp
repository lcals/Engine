#include "plugins/runtime/script/bindings/window_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/window/window_system.hpp"

#include <string>
#include <utility>
#include <vector>

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Window {
    constructor() { throw new TypeError('Window is static'); }
    static GetSize() { return native.WindowGetSize(); }
    static GetDisplayModes() { return native.WindowGetDisplayModes(); }
    static GetMode() { return native.WindowGetMode(); }
    static SetSize(logicalWidth, logicalHeight) {
        return native.WindowSetSize(logicalWidth, logicalHeight);
    }
    static SetMode(mode, exclusiveMode = null) {
        if (mode === 'fullscreen') {
            if (!exclusiveMode || typeof exclusiveMode !== 'object') return false;
            return native.WindowSetMode(mode, exclusiveMode.width, exclusiveMode.height,
                exclusiveMode.pixelDensity, exclusiveMode.refreshRate);
        }
        if (exclusiveMode !== null) return false;
        return native.WindowSetMode(mode);
    }
    static GetMouseMode() { return native.WindowGetMouseMode(); }
    static SetMouseMode(mode) { return native.WindowSetMouseMode(mode); }
}
Object.freeze(Window.prototype);
Object.freeze(Window);
)js";

WindowSystem* window(const Arguments& arguments, int count)
{
    auto* context = BindingContext::current(arguments);
    return context != nullptr && arguments.count() == count ? context->window() : nullptr;
}

void size(const Arguments& arguments)
{
    arguments.set_return({});
    const auto* target = window(arguments, 0);
    WindowSize value;
    if (target != nullptr && target->size(value))
    {
        arguments.set_return(vm::NativeValue::Object{
            {"logicalWidth", value.logical_width}, {"logicalHeight", value.logical_height},
            {"pixelWidth", value.pixel_width}, {"pixelHeight", value.pixel_height}});
    }
}

void display_modes(const Arguments& arguments)
{
    arguments.set_return({});
    const auto* target = window(arguments, 0);
    std::vector<DisplayMode> modes;
    if (target == nullptr || !target->display_modes(modes))
    {
        return;
    }
    vm::NativeValue::Array output;
    output.reserve(modes.size());
    for (const auto& mode : modes)
    {
        output.emplace_back(vm::NativeValue::Object{
            {"width", mode.width}, {"height", mode.height},
            {"pixelDensity", static_cast<double>(mode.pixel_density)},
            {"refreshRate", static_cast<double>(mode.refresh_rate)}});
    }
    arguments.set_return(std::move(output));
}

void mode(const Arguments& arguments)
{
    arguments.set_return({});
    const auto* target = window(arguments, 0);
    WindowMode value;
    if (target == nullptr || !target->mode(value))
    {
        return;
    }
    switch (value)
    {
    case WindowMode::windowed:
        arguments.set_return("windowed");
        return;
    case WindowMode::borderless:
        arguments.set_return("borderless");
        return;
    case WindowMode::fullscreen:
        arguments.set_return("fullscreen");
        return;
    }
}

void set_size(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* target = window(arguments, 2);
    if (target != nullptr && arguments.is_uint32(0) && arguments.is_uint32(1))
    {
        arguments.set_return(target->set_size(arguments.unsigned_integer(0),
                                              arguments.unsigned_integer(1)));
    }
}

void set_mode(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* context = BindingContext::current(arguments);
    auto* target = context != nullptr ? context->window() : nullptr;
    std::string requested;
    if (target == nullptr || !BindingContext::text(arguments, 0, requested))
    {
        return;
    }
    if (requested == "windowed" || requested == "borderless")
    {
        if (arguments.count() == 1)
        {
            arguments.set_return(target->set_mode(requested == "windowed" ? WindowMode::windowed
                                                                          : WindowMode::borderless));
        }
        return;
    }
    DisplayMode value;
    if (requested == "fullscreen" && arguments.count() == 5 && arguments.is_uint32(1) &&
        arguments.is_uint32(2) && BindingContext::scalar(arguments, 3, value.pixel_density) &&
        BindingContext::scalar(arguments, 4, value.refresh_rate))
    {
        value.width = arguments.unsigned_integer(1);
        value.height = arguments.unsigned_integer(2);
        arguments.set_return(target->set_mode(WindowMode::fullscreen, &value));
    }
}

void mouse_mode(const Arguments& arguments)
{
    arguments.set_return({});
    const auto* target = window(arguments, 0);
    MouseMode value;
    if (target == nullptr || !target->mouse_mode(value))
    {
        return;
    }
    switch (value)
    {
    case MouseMode::normal:
        arguments.set_return("normal");
        return;
    case MouseMode::hidden:
        arguments.set_return("hidden");
        return;
    case MouseMode::relative:
        arguments.set_return("relative");
        return;
    }
}

void set_mouse_mode(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* context = BindingContext::current(arguments);
    std::string requested;
    if (context == nullptr || arguments.count() != 1 ||
        !BindingContext::text(arguments, 0, requested))
    {
        return;
    }
    if (requested == "normal" || requested == "hidden" || requested == "relative")
    {
        const auto value = requested == "normal"   ? MouseMode::normal
                           : requested == "hidden" ? MouseMode::hidden
                                                   : MouseMode::relative;
        arguments.set_return(context->set_mouse_mode(value));
    }
}
} // namespace

std::string_view WindowBinding::name() const noexcept
{
    return "Window";
}

std::string_view WindowBinding::source() const noexcept
{
    return javascript;
}

bool WindowBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.function("WindowGetSize", size) &&
           vm.function("WindowGetDisplayModes", display_modes) && vm.function("WindowGetMode", mode) &&
           vm.function("WindowSetSize", set_size) && vm.function("WindowSetMode", set_mode) &&
           vm.function("WindowGetMouseMode", mouse_mode) &&
           vm.function("WindowSetMouseMode", set_mouse_mode);
}

} // namespace engine::plugins::script::bindings
