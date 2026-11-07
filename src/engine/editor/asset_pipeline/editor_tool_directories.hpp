#pragma once

#include <filesystem>
#include <string>

namespace engine::editor::asset_pipeline
{
// 平台实现：无部署包时返回两个空目录；已经部署的目录不能回退本机路径。
[[nodiscard]] bool editor_tool_directories(std::filesystem::path& executables,
                                           std::filesystem::path& data, std::string& error);
} // namespace engine::editor::asset_pipeline
