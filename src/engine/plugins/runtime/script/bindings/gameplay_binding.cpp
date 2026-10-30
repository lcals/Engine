#include "plugins/runtime/script/bindings/gameplay_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Gameplay {
    OnInitialize() { return true; }
    Update() {}
    OnShutdown() {}
}
)js";

} // namespace

std::string_view GameplayBinding::name() const noexcept
{
    return "Gameplay";
}

std::string_view GameplayBinding::source() const noexcept
{
    return javascript;
}

bool GameplayBinding::register_functions(vm::ScriptVm&) const
{
    return true;
}

} // namespace engine::plugins::script::bindings
