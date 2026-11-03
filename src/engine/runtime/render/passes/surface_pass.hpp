#pragma once

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace engine::render_detail
{

// 两阶段深度/表面输出保证无SSR程序的前景物件也能遮挡后方的表面数据。
class SurfacePass final
{
public:
    SurfacePass() = default;
    ~SurfacePass() noexcept;
    SurfacePass(const SurfacePass&) = delete;
    SurfacePass& operator=(const SurfacePass&) = delete;

    [[nodiscard]] bool prepare(std::uint16_t width, std::uint16_t height);
    void begin(const float* view, const float* projection);
    [[nodiscard]] bgfx::TextureHandle depth() const noexcept;
    [[nodiscard]] bgfx::TextureHandle normal_roughness() const noexcept;
    [[nodiscard]] bgfx::TextureHandle specular() const noexcept;
    [[nodiscard]] std::uint32_t resource_count() const noexcept;
    void release() noexcept;

private:
    std::array<bgfx::TextureHandle, 3> targets_{
        {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    bgfx::FrameBufferHandle framebuffer_ = BGFX_INVALID_HANDLE;
    std::uint16_t width_ = 0;
    std::uint16_t height_ = 0;
};

} // namespace engine::render_detail
