#pragma once

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace engine
{

struct RenderSettings;

// RenderSystem 私有通道；GPU 操作在其所属线程、bgfx 初始化期间执行。
class HdrPostProcessPass final
{
public:
    HdrPostProcessPass() = default;
    ~HdrPostProcessPass() noexcept;
    HdrPostProcessPass(const HdrPostProcessPass&) = delete;
    HdrPostProcessPass& operator=(const HdrPostProcessPass&) = delete;
    HdrPostProcessPass(HdrPostProcessPass&&) = delete;
    HdrPostProcessPass& operator=(HdrPostProcessPass&&) = delete;

    [[nodiscard]] bool prepare(std::uint16_t width, std::uint16_t height, bool bloom_enabled);
    // clear_color 沿用显示空间 RGBA8 语义，解码为线性值后参与统一曝光和色调映射。
    void begin_scene(std::uint32_t clear_color);
    [[nodiscard]] bool resolve(const RenderSettings& settings);
    [[nodiscard]] std::uint32_t resource_count() const noexcept;
    void release() noexcept;

private:
    [[nodiscard]] bool initialize_resources();
    void release_bloom() noexcept;
    void release_targets() noexcept;

    std::array<bgfx::ProgramHandle, 3> programs_ = {
        {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    std::array<bgfx::UniformHandle, 2> samplers_ = {{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    bgfx::UniformHandle parameters_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle texel_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle fullscreen_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle black_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle color_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depth_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle framebuffer_ = BGFX_INVALID_HANDLE;
    std::array<bgfx::TextureHandle, 2> bloom_colors_ = {{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    std::array<bgfx::FrameBufferHandle, 2> bloom_framebuffers_ = {
        {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    std::uint16_t width_ = 0;
    std::uint16_t height_ = 0;
    std::array<std::uint16_t, 2> bloom_widths_{};
    std::array<std::uint16_t, 2> bloom_heights_{};
};

} // namespace engine
