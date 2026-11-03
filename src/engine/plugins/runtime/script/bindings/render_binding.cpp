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
    static SetMsaaSamples(samples) { return native.RenderSetMsaaSamples(samples); }
    static GetMsaaSamples() { return native.RenderGetMsaaSamples(); }
    static SupportsMsaaSamples(samples) { return native.RenderSupportsMsaaSamples(samples); }
    static SetMaxAnisotropy(enabled) { return native.RenderSetMaxAnisotropy(enabled); }
    static GetMaxAnisotropy() { return native.RenderGetMaxAnisotropy(); }
    static SupportsMaxAnisotropy() { return native.RenderSupportsMaxAnisotropy(); }
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

bool read_samples(const Arguments& arguments, std::uint8_t& samples)
{
    if (!arguments.is_uint32(0))
    {
        return false;
    }
    const auto value = arguments.unsigned_integer(0);
    if (value != 1 && value != 2 && value != 4 && value != 8)
    {
        return false;
    }
    samples = static_cast<std::uint8_t>(value);
    return true;
}

void set_msaa_samples(const Arguments& arguments)
{
    auto* target = renderer(arguments, 1);
    std::uint8_t samples = 1;
    if (target != nullptr && read_samples(arguments, samples))
    {
        arguments.set_return(target->set_msaa_samples(samples));
    }
}

void msaa_samples(const Arguments& arguments)
{
    auto* target = renderer(arguments, 0);
    arguments.set_return({});
    if (target != nullptr && target->is_initialized())
    {
        arguments.set_return(static_cast<std::uint32_t>(target->msaa_samples()));
    }
}

void supports_msaa_samples(const Arguments& arguments)
{
    auto* target = renderer(arguments, 1);
    std::uint8_t samples = 1;
    if (target != nullptr && read_samples(arguments, samples))
    {
        arguments.set_return(target->supports_msaa_samples(samples));
    }
}

void set_max_anisotropy(const Arguments& arguments)
{
    auto* target = renderer(arguments, 1);
    if (target != nullptr && arguments.is_boolean(0))
    {
        arguments.set_return(target->set_max_anisotropy(arguments.boolean(0)));
    }
}

void max_anisotropy(const Arguments& arguments)
{
    auto* target = renderer(arguments, 0);
    arguments.set_return({});
    if (target != nullptr && target->is_initialized())
    {
        arguments.set_return(target->max_anisotropy());
    }
}

void supports_max_anisotropy(const Arguments& arguments)
{
    auto* target = renderer(arguments, 0);
    if (target != nullptr)
    {
        arguments.set_return(target->supports_max_anisotropy());
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
           vm.function("RenderSetVSync", set_vsync) && vm.function("RenderGetVSync", vsync) &&
           vm.function("RenderSetMsaaSamples", set_msaa_samples) &&
           vm.function("RenderGetMsaaSamples", msaa_samples) &&
           vm.function("RenderSupportsMsaaSamples", supports_msaa_samples) &&
           vm.function("RenderSetMaxAnisotropy", set_max_anisotropy) &&
           vm.function("RenderGetMaxAnisotropy", max_anisotropy) &&
           vm.function("RenderSupportsMaxAnisotropy", supports_max_anisotropy);
}

} // namespace engine::plugins::script::bindings
