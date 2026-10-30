#include "plugins/runtime/script/bindings/engine_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/engine.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Engine {
    constructor() { throw new TypeError('Engine is static'); }
    static SetTargetFrameRate(fps) { return native.EngineSetTargetFrameRate(fps); }
    static GetTargetFrameRate() { return native.EngineGetTargetFrameRate(); }
}
Object.freeze(Engine.prototype);
Object.freeze(Engine);
)js";

void set_target_frame_rate(const Arguments& arguments)
{
    arguments.set_return(false);
    if (BindingContext::current(arguments) == nullptr || arguments.count() != 1 ||
        !arguments.is_int32(0))
    {
        return;
    }
    arguments.set_return(engine::Engine::instance().set_target_frame_rate(arguments.integer(0)));
}

void get_target_frame_rate(const Arguments& arguments)
{
    arguments.set_return({});
    if (BindingContext::current(arguments) == nullptr || arguments.count() != 0)
    {
        return;
    }
    arguments.set_return(engine::Engine::instance().target_frame_rate());
}
} // namespace

std::string_view EngineBinding::name() const noexcept
{
    return "Engine";
}

std::string_view EngineBinding::source() const noexcept
{
    return javascript;
}

bool EngineBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.function("EngineSetTargetFrameRate", set_target_frame_rate) &&
           vm.function("EngineGetTargetFrameRate", get_target_frame_rate);
}

} // namespace engine::plugins::script::bindings
