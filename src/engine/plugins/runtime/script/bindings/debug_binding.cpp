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
    if (arguments.Length() != 1 || !BindingContext::text(arguments, 0, message))
    {
        auto* isolate = arguments.GetIsolate();
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Debug requires one string message")));
        return;
    }
    LogSystem::write(static_cast<LogLevel>(arguments.Data().As<v8::Int32>()->Value()), message);
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

bool DebugBinding::register_functions(v8::Local<v8::Context> context,
                                      v8::Local<v8::Object> native) const
{
    auto* isolate = v8::Isolate::GetCurrent();
    return BindingContext::function(context, native, "Log", log,
                                    v8::Int32::New(isolate, static_cast<int>(LogLevel::info))) &&
           BindingContext::function(context, native, "LogWarning", log,
                                    v8::Int32::New(isolate, static_cast<int>(LogLevel::warning))) &&
           BindingContext::function(context, native, "LogError", log,
                                    v8::Int32::New(isolate, static_cast<int>(LogLevel::error)));
}

} // namespace engine::plugins::script::bindings
