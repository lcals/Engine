#include "plugins/runtime/script/bindings/time_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Time {
    constructor() { throw new TypeError('Time is static'); }
    static get deltaTime() { return native.DeltaTime(); }
}
Object.freeze(Time.prototype);
Object.freeze(Time);
)js";

void delta_time(const Arguments& arguments)
{
    auto* binding = BindingContext::current(arguments);
    arguments.GetReturnValue().Set(binding == nullptr ? 0.0 : binding->delta_time());
}
} // namespace

std::string_view TimeBinding::name() const noexcept
{
    return "Time";
}

std::string_view TimeBinding::source() const noexcept
{
    return javascript;
}

bool TimeBinding::register_functions(v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> native) const
{
    return BindingContext::function(context, native, "DeltaTime", delta_time);
}

} // namespace engine::plugins::script::bindings
