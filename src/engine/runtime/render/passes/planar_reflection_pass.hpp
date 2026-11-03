#pragma once

#include <array>
#include <bgfx/bgfx.h>
#include <cstdint>

namespace engine::render_detail
{

class PlanarReflectionPass final
{
public:
    PlanarReflectionPass() = default;
    ~PlanarReflectionPass() noexcept;
    PlanarReflectionPass(const PlanarReflectionPass&) = delete;
    PlanarReflectionPass& operator=(const PlanarReflectionPass&) = delete;
    [[nodiscard]] bool prepare(std::uint16_t width, std::uint16_t height, std::uint8_t samples);
    [[nodiscard]] bool mirror(const float* plane_world, const float* camera_world,
                              const float* camera_projection, float clip_bias);
    void begin(bgfx::ViewId view_id, std::uint32_t clear_color);
    void release() noexcept;
    [[nodiscard]] std::uint32_t resource_count() const noexcept;
    [[nodiscard]] bgfx::TextureHandle texture() const noexcept
    {
        return color_;
    }
    std::uint64_t node_id = 0;
    std::array<float, 16> world{}, view{}, projection{}, view_projection{};
    std::uint16_t width = 0, height = 0;

private:
    bgfx::TextureHandle color_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle specular_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depth_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle framebuffer_ = BGFX_INVALID_HANDLE;
    bgfx::ViewId attached_view_ = UINT16_MAX;
    std::uint8_t samples_ = 1;
};
} // namespace engine::render_detail
