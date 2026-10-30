#include "runtime/resource/ktx_texture.hpp"

#include <bimg/bimg.h>
#include <bx/error.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace engine::resource_detail
{
namespace
{

std::uint32_t uint32_at(ByteView bytes, std::size_t offset) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

} // namespace

bool read_ktx_info(ByteView bytes, KtxTextureInfo& output)
{
    output = {};
    constexpr std::array<std::uint8_t, 12> identifier{0xab, 0x4b, 0x54, 0x58, 0x20, 0x31,
                                                      0x31, 0xbb, 0x0d, 0x0a, 0x1a, 0x0a};
    if (bytes.size() < 64 || bytes.size() > maximum_ktx_size ||
        !std::equal(identifier.begin(), identifier.end(), bytes.begin(),
                    [](std::uint8_t a, std::byte b)
                    { return a == std::to_integer<std::uint8_t>(b); }))
    {
        return false;
    }
    // 当前资源只接入标准小端 KTX1、无压缩半浮点二维或立方体；先检查原始
    // 头字段，防止上游将非法面数/大整数截断为看似有效的容器信息。
    const auto format = uint32_at(bytes, 28);
    const bool rgba = format == 0x881aU;
    const bool rg = format == 0x822fU;
    const std::uint32_t channels = rgba ? 4U : 2U;
    const auto width = uint32_at(bytes, 36);
    const auto height = uint32_at(bytes, 40);
    const auto faces = uint32_at(bytes, 52);
    const auto mips = uint32_at(bytes, 56);
    const auto metadata_size = uint32_at(bytes, 60);
    const bool typed_pixels = uint32_at(bytes, 16) == 0x140bU && uint32_at(bytes, 20) == 2 &&
                              uint32_at(bytes, 24) == (rgba ? 0x1908U : 0x8227U);
    // 当前 bimg 写入器统一采用这组字段；实际半浮点布局由 internalFormat 决定。
    const bool bimg_pixels =
        uint32_at(bytes, 16) == 0 && uint32_at(bytes, 20) == 1 && uint32_at(bytes, 24) == 0;
    if (uint32_at(bytes, 12) != 0x04030201U || (!typed_pixels && !bimg_pixels) || (!rgba && !rg) ||
        uint32_at(bytes, 32) != (rgba ? 0x1908U : 0x8227U) || width == 0 || height == 0 ||
        width > 16384 || height > 16384 || uint32_at(bytes, 44) != 0 || uint32_at(bytes, 48) != 0 ||
        (faces != 1 && faces != 6) || (faces == 6 && width != height) || mips == 0 ||
        metadata_size % 4 != 0 || metadata_size > bytes.size() - 64)
    {
        return false;
    }
    std::uint32_t full_mips = 1;
    for (auto dimension = std::max(width, height); dimension > 1; dimension >>= 1)
    {
        ++full_mips;
    }
    if (mips != 1 && mips != full_mips)
    {
        return false;
    }
    std::size_t offset = 64 + metadata_size;
    for (std::uint32_t mip = 0; mip < mips; ++mip)
    {
        const auto face_bytes = static_cast<std::uint64_t>(std::max(1U, width >> mip)) *
                                std::max(1U, height >> mip) * channels * 2U;
        if (offset > bytes.size() || bytes.size() - offset < 4 ||
            uint32_at(bytes, offset) != face_bytes)
        {
            return false;
        }
        offset += 4;
        const auto mip_bytes = face_bytes * faces;
        if (mip_bytes > bytes.size() - offset)
        {
            return false;
        }
        offset += static_cast<std::size_t>(mip_bytes);
    }
    if (offset != bytes.size())
    {
        return false;
    }
    bimg::ImageContainer image{};
    bx::Error error;
    if (!bimg::imageParse(image, bytes.data(), static_cast<std::uint32_t>(bytes.size()), &error) ||
        !error.isOk() || !image.m_ktx || !image.m_ktxLE || image.m_srgb || image.m_width != width ||
        image.m_height != height || image.m_depth != 1 || image.m_numLayers != 1 ||
        image.m_numMips != mips || image.m_cubeMap != (faces == 6) ||
        image.m_format != (rgba ? bimg::TextureFormat::RGBA16F : bimg::TextureFormat::RG16F))
    {
        return false;
    }
    output.width = width;
    output.height = height;
    output.mip_count = mips;
    output.cube_map = faces == 6;
    output.format = rgba ? schema::TextureFormat::rgba16f : schema::TextureFormat::rg16f;
    return true;
}

} // namespace engine::resource_detail
