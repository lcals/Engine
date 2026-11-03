#pragma once

#include <array>
#include <bgfx/bgfx.h>
#include <cstdint>

namespace engine
{
struct RenderSettings;
}

namespace engine::render_detail
{
class ScreenSpaceReflectionPass final
{
public:
    ScreenSpaceReflectionPass();
    ~ScreenSpaceReflectionPass() noexcept;
    ScreenSpaceReflectionPass(const ScreenSpaceReflectionPass&) = delete;
    ScreenSpaceReflectionPass& operator=(const ScreenSpaceReflectionPass&) = delete;

    [[nodiscard]] bool prepare(std::uint16_t width, std::uint16_t height,
                               const RenderSettings& settings);
    [[nodiscard]] bool render(const float* view, const float* projection,
                              bgfx::TextureHandle scene_color, bgfx::TextureHandle ibl_specular,
                              bgfx::TextureHandle depth, bgfx::TextureHandle normal_roughness,
                              bgfx::TextureHandle specular, bgfx::TextureHandle brdf_lut,
                              const RenderSettings& settings);
    [[nodiscard]] bgfx::TextureHandle texture() const noexcept;
    void release() noexcept;
    [[nodiscard]] std::uint32_t resource_count() const noexcept;

private:
    std::array<bgfx::ProgramHandle, 3> programs_;
    std::array<bgfx::UniformHandle, 8> samplers_;
    std::array<bgfx::UniformHandle, 8> uniforms_;
    std::array<bgfx::TextureHandle, 2> hierarchy_;
    std::array<bgfx::FrameBufferHandle, 12> hierarchy_frames_;
    bgfx::TextureHandle reflection_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle color_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle reflection_frame_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle color_frame_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle fullscreen_ = BGFX_INVALID_HANDLE;
    std::uint16_t width_ = 0;
    std::uint16_t height_ = 0;
    std::uint16_t trace_width_ = 0;
    std::uint16_t trace_height_ = 0;
    std::uint16_t levels_ = 0;
};
} // namespace engine::render_detail
