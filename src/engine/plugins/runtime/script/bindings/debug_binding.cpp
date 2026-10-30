#include "plugins/runtime/script/bindings/debug_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/log/log_system.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Debug {
    constructor() { throw new TypeError('Debug is static'); }
    static Log(message) { native.Log(message); }
    static LogWarning(message) { native.LogWarning(message); }
    static LogError(message) { native.LogError(message); }
}
Object.freeze(Debug.prototype);
Object.freeze(Debug);
)js";

void log(const Arguments& arguments)
{
    if (BindingContext::current(arguments, false) == nullptr)
    {
        return;
    }
    std::string message;
    if (arguments.count() != 1 || !BindingContext::text(arguments, 0, message))
    {
        arguments.type_error("Debug requires one string message");
        return;
    }
    LogSystem::write(static_cast<LogLevel>(arguments.data()), message);
}
} // namespace

std::string_view DebugBinding::name() const noexcept
{
    return "Debug";
}

std::string_view DebugBinding::source() const noexcept
{
    return javascript;
}

bool DebugBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.function("Log", log, static_cast<std::int32_t>(LogLevel::info)) &&
           vm.function("LogWarning", log, static_cast<std::int32_t>(LogLevel::warning)) &&
           vm.function("LogError", log, static_cast<std::int32_t>(LogLevel::error));
}

} // namespace engine::plugins::script::bindings
