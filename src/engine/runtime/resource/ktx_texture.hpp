#pragma once

#include "runtime/resource/resource.hpp"

#include <cstdint>

namespace engine::resource_detail
{

// 仅描述已检查的容器头，不是另一份持久资源模型。
struct KtxTextureInfo
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mip_count = 0;
    bool cube_map = false;
    schema::TextureFormat format = schema::TextureFormat::auto_format;
};

// 为资源实体头与 FlatBuffers 留出空间；通用单实体读取上限为 64 MiB。
inline constexpr std::uint32_t maximum_ktx_size = 63U * 1024U * 1024U;
[[nodiscard]] bool read_ktx_info(ByteView bytes, KtxTextureInfo& output);

} // namespace engine::resource_detail
