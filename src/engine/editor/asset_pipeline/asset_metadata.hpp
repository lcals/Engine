#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace engine::asset_pipeline
{

// 调用方持有项目构建锁；缺失的已知资源 meta 不得重新分配身份。
[[nodiscard]] bool read_or_create_asset_meta(const std::filesystem::path& source,
                                             const std::filesystem::path& staging,
                                             std::string_view importer, std::string_view defaults,
                                             bool allow_create, std::string& text,
                                             std::string& guid, std::string& error);

// 为编辑器发布源资源提供同一身份契约；不导入、不更新运行目录。
[[nodiscard]] bool ensure_asset_guid(const std::filesystem::path& project_root,
                                     const std::filesystem::path& source, std::string_view importer,
                                     std::string& guid, std::string& error);

} // namespace engine::asset_pipeline
