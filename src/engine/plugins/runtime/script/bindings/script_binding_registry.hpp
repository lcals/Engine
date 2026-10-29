#pragma once

#include "plugins/runtime/script/bindings/script_binding.hpp"

#include <concepts>
#include <memory>
#include <string>
#include <vector>

namespace engine::plugins::script::bindings
{

class ScriptBindingRegistry final
{
public:
    [[nodiscard]] bool initialize();
    template <std::derived_from<ScriptBinding> T> [[nodiscard]] bool add()
    {
        std::unique_ptr<ScriptBinding> binding = std::make_unique<T>();
        return add_binding(binding);
    }
    // 成功才转移所有权；拒绝空绑定和重复名称。
    [[nodiscard]] bool add_binding(std::unique_ptr<ScriptBinding>& binding);
    [[nodiscard]] bool register_functions(v8::Local<v8::Context> context) const;
    [[nodiscard]] std::string source() const;

private:
    std::vector<std::unique_ptr<ScriptBinding>> bindings_;
};

} // namespace engine::plugins::script::bindings
