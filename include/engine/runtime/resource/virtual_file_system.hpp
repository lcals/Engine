#pragma once

#include "runtime/schema/asset_catalog_generated.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine
{

using ByteBuffer = std::vector<std::byte>;

struct ResourceMount
{
    std::string name;
    std::filesystem::path root;
};

using MountList = std::vector<ResourceMount>;

class VirtualFileSystem final
{
public:
    // 仅配置路径，不扫描或打开文件；失败保留旧挂载。
    [[nodiscard]] bool configure(const MountList& mounts);
    [[nodiscard]] bool read(const schema::FileLocationT& location, std::uint64_t offset,
                            std::uint64_t size, ByteBuffer& output) const;
    [[nodiscard]] static bool read_file(const std::filesystem::path& path, ByteBuffer& output);
    // 固定 64 KiB 缓冲计算完整文件 FNV-1a 64 位摘要；失败两项输出清零。
    [[nodiscard]] static bool hash_file(const std::filesystem::path& path, std::uint64_t& hash,
                                        std::uint64_t& size);
    [[nodiscard]] const MountList& mounts() const noexcept;

private:
    MountList mounts_;
};

} // namespace engine
