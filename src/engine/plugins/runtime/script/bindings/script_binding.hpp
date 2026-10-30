#pragma once

#include "plugins/runtime/script/vm/script_vm.hpp"

#include <string_view>

namespace engine::plugins::script::bindings
{

class ScriptBinding
{
public:
    virtual ~ScriptBinding() noexcept = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::string_view source() const noexcept = 0;
    [[nodiscard]] virtual bool register_functions(vm::ScriptVm& vm) const = 0;
};

} // namespace engine::plugins::script::bindings
