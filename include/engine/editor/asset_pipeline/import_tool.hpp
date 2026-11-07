#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace engine::editor::asset_pipeline
{
using CancellationCheck = std::function<bool()>;

enum class ImportToolStatus
{
    success,
    failed,
    cancelled,
};

[[nodiscard]] bool find_import_tool(std::string_view name, std::filesystem::path& output,
                                    std::string& error);
// 同步执行并回收本次进程。取消条件仅在本次调用期间借用；默认不额外限制输出。
[[nodiscard]] ImportToolStatus
run_import_tool(const std::vector<std::string>& arguments, std::string& output, std::string& error,
                const CancellationCheck& cancel_requested = {},
                std::size_t max_output_bytes = std::numeric_limits<std::size_t>::max());
} // namespace engine::editor::asset_pipeline
