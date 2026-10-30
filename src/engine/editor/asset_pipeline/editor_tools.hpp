#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace engine::editor::asset_pipeline
{
// 部署后的离线工具与数据优先；仅未部署工具包的开发程序使用构建路径。
[[nodiscard]] bool editor_tool_path(std::string_view name, const std::filesystem::path& fallback,
                                    std::filesystem::path& path, std::string& error);
[[nodiscard]] bool editor_tool_data_path(std::string_view relative,
                                         const std::filesystem::path& fallback,
                                         std::filesystem::path& path, std::string& error);

// 平台实现：无部署包时返回两个空目录；已经部署的目录不能回退本机路径。
[[nodiscard]] bool editor_tool_directories(std::filesystem::path& executables,
                                           std::filesystem::path& data, std::string& error);
} // namespace engine::editor::asset_pipeline
