#pragma once

#include <bgfx/bgfx.h>

#include <cstddef>
#include <cstdint>

// 渲染通道及Shader保留槽；新增通道在此登记，顺序不可与UI及编辑器GPU作业冲突。

namespace engine::render_detail
{

inline constexpr std::uint16_t shadow_view = 0;
inline constexpr std::uint16_t maximum_shadow_cascades = 4;
inline constexpr std::uint16_t ambient_occlusion_depth_view = 4;
inline constexpr std::uint16_t main_view = 6;
inline constexpr std::uint8_t shadow_sampler_stage = 15;
inline constexpr std::uint8_t ambient_occlusion_stage = 11;
inline constexpr std::uint8_t ibl_irradiance_stage = 12;
inline constexpr std::uint8_t ibl_prefilter_stage = 13;
inline constexpr std::uint8_t ibl_lut_stage = 14;
inline constexpr std::uint32_t reset_flags = BGFX_RESET_VSYNC;
inline constexpr std::size_t maximum_lights = 64;

inline constexpr std::uint16_t ambient_occlusion_compute_view = 5;
inline constexpr std::uint16_t hdr_bright_view = 7;
inline constexpr std::uint16_t hdr_blur_view = 8;
inline constexpr std::uint16_t hdr_resolve_view = 9;

} // namespace engine::render_detail
