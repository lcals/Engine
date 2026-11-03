#pragma once

#include <bgfx/bgfx.h>

#include <cstddef>
#include <cstdint>

// 渲染通道及Shader保留槽；新增通道在此登记，顺序不可与UI及编辑器GPU作业冲突。

namespace engine::render_detail
{

inline constexpr std::uint16_t skinning_view = 0;
inline constexpr std::uint16_t shadow_view = 80;
inline constexpr std::uint16_t maximum_shadow_cascades = 4;
inline constexpr std::uint16_t ambient_occlusion_depth_view = 110;
inline constexpr std::uint16_t surface_view = 111;
inline constexpr std::uint16_t main_view = 113;
inline constexpr std::uint16_t ssr_hiz_view = 114;
inline constexpr std::uint16_t ssr_trace_view = 126;
inline constexpr std::uint16_t ssr_composite_view = 127;
inline constexpr std::uint16_t scene_copy_view = 128;
inline constexpr std::uint16_t transparent_view = 130;
inline constexpr std::uint16_t extension_view = 134;
inline constexpr std::uint8_t shadow_sampler_stage = 15;
inline constexpr std::uint8_t ambient_occlusion_stage = 11;
inline constexpr std::uint8_t ibl_irradiance_stage = 12;
inline constexpr std::uint8_t ibl_prefilter_stage = 13;
inline constexpr std::uint8_t ibl_lut_stage = 14;
inline constexpr std::uint32_t reset_flags = BGFX_RESET_NONE;

inline constexpr std::uint16_t ambient_occlusion_compute_view = 112;
inline constexpr std::uint16_t hdr_bright_view = 135;
inline constexpr std::uint16_t hdr_blur_view = 136;
inline constexpr std::uint16_t hdr_resolve_view = 137;
// Capture lighting runs after the main image, before UI (200) and bake IBL views (240/241).
inline constexpr std::uint16_t capture_shadow_view = 144;
inline constexpr std::uint16_t capture_view = 242;
inline constexpr std::uint16_t capture_copy_view = 243;

} // namespace engine::render_detail
