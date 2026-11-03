#include "plugins/runtime/script/bindings/render_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/render/render_system.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Render {
    constructor() { throw new TypeError('Render is static'); }
    static GetViewportSize() { return native.RenderGetViewportSize(); }
    static SetVSync(enabled) { return native.RenderSetVSync(enabled); }
    static GetVSync() { return native.RenderGetVSync(); }
}
Object.freeze(Render.prototype);
Object.freeze(Render);
)js";

RenderSystem* renderer(const Arguments& arguments, int count)
{
    arguments.set_return(false);
    auto* binding = BindingContext::current(arguments);
    return binding && arguments.count() == count ? binding->renderer() : nullptr;
}

void viewport_size(const Arguments& arguments)
{
    arguments.set_return({});
    auto* binding = BindingContext::current(arguments);
    const auto* target = binding != nullptr ? binding->renderer() : nullptr;
    Vec2 size;
    if (target != nullptr && arguments.count() == 0 && target->viewport_size(size))
    {
        arguments.set_return(vm::NativeValue::Object{{"x", size.x()}, {"y", size.y()}});
    }
}

void set_vsync(const Arguments& arguments)
{
    auto* target = renderer(arguments, 1);
    if (target != nullptr && arguments.is_boolean(0))
    {
        arguments.set_return(target->set_vsync(arguments.boolean(0)));
    }
}

void vsync(const Arguments& arguments)
{
    arguments.set_return({});
    auto* binding = BindingContext::current(arguments);
    const auto* target = binding != nullptr ? binding->renderer() : nullptr;
    if (target != nullptr && target->is_initialized() && arguments.count() == 0)
    {
        arguments.set_return(target->vsync());
    }
}

} // namespace

std::string_view RenderBinding::name() const noexcept
{
    return "Render";
}

std::string_view RenderBinding::source() const noexcept
{
    return javascript;
}

bool RenderBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.function("RenderGetViewportSize", viewport_size) &&
           vm.function("RenderSetVSync", set_vsync) && vm.function("RenderGetVSync", vsync);
}

} // namespace engine::plugins::script::bindings
