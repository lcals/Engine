#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace engine
{

class PathUtils final
{
public:
    // 只转换编码，不修改路径内容，也不执行规范化或安全校验。
    [[nodiscard]] static std::filesystem::path from_utf8(std::string_view text);
    // 使用 generic 路径格式，目录分隔符统一为正斜杠。
    [[nodiscard]] static std::string to_utf8(const std::filesystem::path& path);

private:
    PathUtils() = delete;
};

} // namespace engine
