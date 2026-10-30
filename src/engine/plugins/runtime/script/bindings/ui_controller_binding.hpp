#pragma once

#include "plugins/runtime/script/bindings/script_binding.hpp"

namespace engine::plugins::script::bindings
{

class UiControllerBinding final : public ScriptBinding
{
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view source() const noexcept override;
    [[nodiscard]] bool register_functions(v8::Local<v8::Context> context,
                                          v8::Local<v8::Object> native) const override;
};

} // namespace engine::plugins::script::bindings
