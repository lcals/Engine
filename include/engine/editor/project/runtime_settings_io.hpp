#pragma once

#include "runtime/app/runtime_settings.hpp"

namespace engine::editor
{

// 项目源配置只在编辑器读取；合并当前平台的完整配置，缺项或无效时失败。
[[nodiscard]] bool load_project_runtime_settings(const std::filesystem::path& project_root,
                                                 std::string_view platform, RuntimeSettings& output,
                                                 std::string& error);

} // namespace engine::editor
