#pragma once

#include <cstdint>
#include <string_view>

namespace engine
{

#if defined(ENGINE_ENABLE_TEST_SUPPORT)
// 字符串仅在同步执行期间借用；javascript 是工具已编译的 CommonJS 函数体。
struct ProjectScriptRequest
{
    std::uint64_t expected_run_id = 0;
    std::string_view module_path;
    std::string_view javascript;
    std::string_view arguments_json;
};

#endif

} // namespace engine
