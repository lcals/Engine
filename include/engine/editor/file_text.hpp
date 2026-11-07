#pragma once

#include "runtime/resource/virtual_file_system.hpp"

#include <string>

namespace engine::editor::file_detail
{
// 编辑器源文件不是运行资源；只读取物理文件并复制文本。
[[nodiscard]] inline bool read_text(const std::filesystem::path& path, std::string& output)
{
    output.clear();
    ByteBuffer bytes;
    if (!VirtualFileSystem::read_file(path, bytes))
    {
        return false;
    }
    if (!bytes.empty())
    {
        output.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    return true;
}
} // namespace engine::editor::file_detail
