#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

namespace engine::render_detail
{

[[nodiscard]] constexpr bool valid_msaa_samples(std::uint8_t samples) noexcept
{
    return samples == 1 || samples == 2 || samples == 4 || samples == 8;
}

// BGFX_TEXTURE_RT_MSAA_* 已包含 RT 位域，不能再与 BGFX_TEXTURE_RT 按位或。
[[nodiscard]] constexpr std::uint64_t render_target_flags(std::uint8_t samples) noexcept
{
    switch (samples)
    {
    case 2:
        return BGFX_TEXTURE_RT_MSAA_X2;
    case 4:
        return BGFX_TEXTURE_RT_MSAA_X4;
    case 8:
        return BGFX_TEXTURE_RT_MSAA_X8;
    default:
        return BGFX_TEXTURE_RT;
    }
}

} // namespace engine::render_detail
