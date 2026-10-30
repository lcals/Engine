#pragma once

#include "plugins/runtime/script/bindings/script_binding.hpp"

namespace engine::plugins::script::bindings
{

class NodeBinding final : public ScriptBinding
{
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view source() const noexcept override;
    [[nodiscard]] bool register_functions(vm::ScriptVm& vm) const override;
};

} // namespace engine::plugins::script::bindings
