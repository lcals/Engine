#pragma once

#include "runtime/resource/virtual_file_system.hpp"
#include <string>

namespace test_support
{
inline bool read_text(const std::filesystem::path& path, std::string& output)
{
    output.clear();
    engine::ByteBuffer bytes;
    if (!engine::VirtualFileSystem::read_file(path, bytes))
    {
        return false;
    }
    if (!bytes.empty())
    {
        output.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    return true;
}
} // namespace test_support
