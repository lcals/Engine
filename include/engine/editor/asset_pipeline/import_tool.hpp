#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
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
[[nodiscard]] ImportToolStatus run_import_tool(const std::vector<std::string>& arguments,
                                               const CancellationCheck& cancel_requested,
                                               std::size_t max_output_bytes, std::string& output,
                                               std::string& error);
} // namespace engine::editor::asset_pipeline
