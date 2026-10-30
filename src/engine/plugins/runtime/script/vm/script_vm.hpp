#pragma once

#include "plugins/runtime/script/vm/arguments.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#if defined(ENGINE_SCRIPT_BACKEND_QUICKJS) == defined(ENGINE_SCRIPT_BACKEND_V8)
#error Exactly one script backend must be selected
#endif

namespace engine::plugins::script::vm
{

using NativeCallback = void (*)(const Arguments& arguments);
using SourceLoader = std::function<bool(std::string_view path, std::string& source)>;

class ScriptVm
{
public:
    virtual ~ScriptVm() noexcept = default;
    [[nodiscard]] virtual bool initialize(void* owner) = 0;
    [[nodiscard]] virtual bool function(std::string_view name, NativeCallback callback,
                                        std::int32_t data = 0) = 0;
    [[nodiscard]] virtual bool integer(std::string_view name, std::int32_t value) = 0;
    [[nodiscard]] virtual bool string(std::string_view name, std::string_view value) = 0;
    [[nodiscard]] virtual bool seal_bindings() = 0;
    [[nodiscard]] virtual bool prepare(std::string_view entry, SourceLoader loader) = 0;
    [[nodiscard]] virtual bool start() = 0;
    [[nodiscard]] virtual bool call_gameplay(std::string_view name, bool require_true) = 0;
    [[nodiscard]] virtual bool call_binding(std::string_view name) = 0;
    [[nodiscard]] virtual bool drain_jobs() = 0;
    [[nodiscard]] virtual const std::string& last_error() const noexcept = 0;
    [[nodiscard]] virtual const std::vector<std::string>& loaded_modules() const noexcept = 0;
};

// 由互斥的构建宏选择实现；调用方独占返回对象，initialize 失败时同样可安全销毁。
[[nodiscard]] std::unique_ptr<ScriptVm> create_script_vm();
[[nodiscard]] bool resolve_module_path(std::string_view referrer, std::string_view request,
                                       std::string& output);

inline constexpr std::size_t maximum_text_size = 4 * 1024 * 1024;
inline constexpr std::size_t maximum_module_count = 256;

} // namespace engine::plugins::script::vm
