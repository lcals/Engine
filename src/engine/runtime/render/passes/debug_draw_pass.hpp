#pragma once

#include "runtime/render/render_features.hpp"
#include "runtime/scene/camera.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <span>

namespace engine
{
class WorldSystem;
}

namespace engine::render_detail
{
struct GpuMesh;

class DebugDrawPass final
{
public:
    DebugDrawPass() = default;
    ~DebugDrawPass() noexcept;
    DebugDrawPass(const DebugDrawPass&) = delete;
    DebugDrawPass& operator=(const DebugDrawPass&) = delete;
    [[nodiscard]] static bool supports_wireframe() noexcept;
    [[nodiscard]] bool
    draw(const WorldSystem& world, Camera camera, std::span<const GpuMesh> meshes,
         bgfx::FrameBufferHandle framebuffer, std::uint16_t width, std::uint16_t height,
         const float* view, const float* projection,
         const std::array<bool, static_cast<std::size_t>(RenderFeature::count)>& features,
         std::span<const DebugLine> lines, std::span<const std::size_t> lod_levels);
    void reset_frame() noexcept;
    [[nodiscard]] bool submitted(RenderFeature feature) const noexcept;
    [[nodiscard]] std::uint32_t resource_count() const noexcept;
    void release() noexcept;

private:
    [[nodiscard]] bool prepare(bool wireframe);
    bgfx::ProgramHandle wireframe_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle parameters_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle color_ = BGFX_INVALID_HANDLE;
    bool initialized_ = false;
    std::array<bool, static_cast<std::size_t>(RenderFeature::count)> submitted_{};
};
} // namespace engine::render_detail
