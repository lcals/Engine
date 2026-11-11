#pragma once

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace engine::storage_detail
{

// 成功返回新文件的独占流，由调用方 fclose；已有路径失败且保持原内容。
[[nodiscard]] std::FILE* open_exclusive_file(const std::filesystem::path& path,
                                             std::error_code& error);

// 输入为已写入并关闭的同目录临时文件；失败保留旧目标，成功消耗临时文件。
[[nodiscard]] bool publish_storage_file(const std::filesystem::path& temporary,
                                        const std::filesystem::path& target, std::string& error);

} // namespace engine::storage_detail
