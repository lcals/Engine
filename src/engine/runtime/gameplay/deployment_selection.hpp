#pragma once

#include "runtime/resource/resource_catalog.hpp"
#include "runtime/resource/virtual_file_system.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace engine::gameplay_detail
{
// 只验证已发布的完整版本，不提交目录；无发布版本时输出空目录。
[[nodiscard]] bool select_published_deployment(const std::filesystem::path& persistent_root,
                                               std::string_view target_platform,
                                               ResourceCatalog& catalog, MountList& mounts,
                                               std::string& revision, std::string& entry,
                                               std::string& error);
} // namespace engine::gameplay_detail
