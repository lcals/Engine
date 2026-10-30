#pragma once

#include <bimg/bimg.h>
#include <bx/error.h>
#include <bx/readerwriter.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace test_support
{

inline std::string ktx_image(bool cube, bool rgba, std::uint8_t mip_count)
{
    std::size_t pixel_bytes = 0;
    for (std::uint8_t mip = 0; mip < mip_count; ++mip)
    {
        const auto size = std::max(1U, 4U >> mip);
        pixel_bytes += size * size * (rgba ? 8U : 4U) * (cube ? 6U : 1U);
    }
    std::vector<std::uint8_t> pixels(pixel_bytes);
    for (std::size_t index = 0; index < pixels.size(); index += 2)
    {
        pixels[index] = 0;
        pixels[index + 1] = 0x40; // half 2.0，确保 HDR 数值不会被 LDR 导入截断。
    }
    std::string output(64 + mip_count * 4 + pixel_bytes, '\0');
    bx::StaticMemoryBlockWriter writer(output.data(), static_cast<std::uint32_t>(output.size()));
    bx::Error error;
    const auto written = bimg::imageWriteKtx(
        &writer, rgba ? bimg::TextureFormat::RGBA16F : bimg::TextureFormat::RG16F, cube, 4, 4, 1,
        mip_count, 1, false, pixels.data(), &error);
    return error.isOk() && written == static_cast<std::int32_t>(output.size()) ? output
                                                                               : std::string{};
}

} // namespace test_support
