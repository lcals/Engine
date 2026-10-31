#pragma once

#include "runtime/scene/scene_types.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace engine
{
struct RenderSettings;
}

namespace engine::render_detail
{

// RenderSystem 私有通道；GPU 操作在其所属线程、bgfx 初始化期间执行。
class AmbientOcclusionPass final
{
public:
    AmbientOcclusionPass() = default;
    ~AmbientOcclusionPass() noexcept;
    AmbientOcclusionPass(const AmbientOcclusionPass&) = delete;
    AmbientOcclusionPass& operator=(const AmbientOcclusionPass&) = delete;
    AmbientOcclusionPass(AmbientOcclusionPass&&) = delete;
    AmbientOcclusionPass& operator=(AmbientOcclusionPass&&) = delete;

    [[nodiscard]] bool prepare(std::uint16_t width, std::uint16_t height);
    void begin_depth(const float* view, const float* projection);
    [[nodiscard]] bool compute(const CameraSettings& camera, const RenderSettings& settings,
                               const float* projection);
    [[nodiscard]] bgfx::TextureHandle texture() const noexcept;
    [[nodiscard]] std::uint32_t resource_count() const noexcept;
    void release() noexcept;

private:
    [[nodiscard]] bool initialize_programs();
    void release_textures() noexcept;

    std::array<bgfx::ProgramHandle, 5> programs_ = {{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
                                                     BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
                                                     BGFX_INVALID_HANDLE}};
    std::array<bgfx::UniformHandle, 5> samplers_ = {{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
                                                     BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
                                                     BGFX_INVALID_HANDLE}};
    bgfx::UniformHandle parameters_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle rectangle_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle depth_framebuffer_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depth_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle normals_ = BGFX_INVALID_HANDLE;
    std::array<bgfx::TextureHandle, 4> half_depths_ = {
        {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    std::array<bgfx::TextureHandle, 2> ping_pong_ = {{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    bgfx::TextureHandle final_results_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ao_ = BGFX_INVALID_HANDLE;
    std::uint16_t width_ = 0;
    std::uint16_t height_ = 0;
};

} // namespace engine::render_detail
