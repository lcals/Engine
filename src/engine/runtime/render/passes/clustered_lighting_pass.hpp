#pragma once

#include "runtime/render/passes/shadow_frame.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <span>

namespace engine
{
struct RenderConfig;
}

namespace engine::render_detail
{

// One instance per camera submission. A later camera must not update buffers
// already referenced by an earlier camera in the same bgfx frame.
class ClusteredLightingPass final
{
public:
    ClusteredLightingPass() = default;
    ~ClusteredLightingPass() noexcept;
    ClusteredLightingPass(const ClusteredLightingPass&) = delete;
    ClusteredLightingPass& operator=(const ClusteredLightingPass&) = delete;

    [[nodiscard]] bool prepare(const RenderConfig& config, std::uint16_t width,
                               std::uint16_t height);
    [[nodiscard]] bool cull(bgfx::ViewId view, const CameraSettings& camera,
                            const float* view_matrix, const float* projection,
                            std::span<const ClusterLight> lights,
                            std::span<const ShadowRecord> shadows);
    void bind_view(bgfx::ViewId view) const;
    void bind() const;
    void release() noexcept;
    [[nodiscard]] std::uint32_t resource_count() const noexcept;

private:
    bgfx::DynamicVertexBufferHandle lights_ = BGFX_INVALID_HANDLE;
    bgfx::DynamicIndexBufferHandle clusters_ = BGFX_INVALID_HANDLE;
    bgfx::ShaderHandle shader_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle grid_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle camera_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle counts_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle viewport_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle view_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle inverse_projection_uniform_ = BGFX_INVALID_HANDLE;
    std::array<float, 4> grid_{};
    std::array<float, 4> camera_{};
    std::array<float, 4> counts_{};
    std::array<float, 4> viewport_{};
    std::uint32_t cluster_count_ = 0;
    std::uint32_t maximum_lights_ = 0;
    std::uint16_t maximum_cluster_lights_ = 0;
    std::uint16_t maximum_shadow_faces_ = 0;
    bool ready_ = false;
};

} // namespace engine::render_detail
