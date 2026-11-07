#pragma once

#include "runtime/render/resources/gpu_geometry.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <memory>

namespace engine
{
class MeshRenderer;
namespace schema
{
struct ShaderDataT;
}
} // namespace engine

namespace engine::render_detail
{

class SkinningPass;

// 实例仅独占姿态和输出缓冲；不可变几何共享。渲染线程在 bgfx 关闭前释放。
struct GpuMesh
{
    GpuMesh() = default;
    ~GpuMesh() noexcept;
    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;
    GpuMesh(GpuMesh&& other) noexcept;
    GpuMesh& operator=(GpuMesh&& other) noexcept;

    [[nodiscard]] bool create(const MeshRenderer& renderer, std::shared_ptr<GpuGeometry> source);
    [[nodiscard]] bool update_deformation(const MeshRenderer& renderer, SkinningPass& skinning);
    void bind_vertices() const;
    void release() noexcept;

    std::uint64_t node_id = 0;
    std::uint64_t instance_id = 0;
    std::uint64_t revision = 0;
    std::uint64_t deformation_revision = 0;
    std::shared_ptr<GpuGeometry> geometry;
    std::vector<const Resource*> textures;
    std::vector<const schema::ShaderDataT*> programs;
    bgfx::DynamicVertexBufferHandle deformed_vertices = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle palette = BGFX_INVALID_HANDLE;
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};

private:
    bool skinned_ = false;
    std::uint32_t joint_count_ = 0;
    void release_deformation() noexcept;
};

} // namespace engine::render_detail
