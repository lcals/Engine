#pragma once

#include "runtime/app/runtime_settings.hpp"

namespace engine::editor
{

// 项目源配置只在编辑器读取；缺少文件保留调用方默认，文件存在但无效则失败。
[[nodiscard]] bool load_project_runtime_settings(const std::filesystem::path& project_root,
                                                 std::string_view profile, RuntimeSettings& output,
                                                 std::string& error);

} // namespace engine::editor
