#pragma once

#include <cstdio>
#include <filesystem>
#include <system_error>

namespace engine::storage_detail
{

// 成功返回新文件的独占流，由调用方 fclose；已有路径失败且保持原内容。
[[nodiscard]] std::FILE* open_exclusive_file(const std::filesystem::path& path,
                                             std::error_code& error);

} // namespace engine::storage_detail
