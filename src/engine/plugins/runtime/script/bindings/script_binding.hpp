#pragma once

#include <v8.h>

#include <string_view>

namespace engine::plugins::script::bindings
{

class ScriptBinding
{
public:
    virtual ~ScriptBinding() noexcept = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::string_view source() const noexcept = 0;
    [[nodiscard]] virtual bool register_functions(v8::Local<v8::Context> context,
                                                  v8::Local<v8::Object> native) const = 0;
};

} // namespace engine::plugins::script::bindings
