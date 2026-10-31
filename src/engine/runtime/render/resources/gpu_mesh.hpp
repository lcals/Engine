#pragma once

#include "runtime/render/resources/gpu_vertex.hpp"
#include "runtime/schema/mesh_generated.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace engine
{
class MeshRenderer;
}

namespace engine::render_detail
{

// 独占静态顶点、变形顶点和索引缓冲；渲染线程在 bgfx 关闭前释放。
struct GpuMesh
{
    GpuMesh() = default;
    ~GpuMesh() noexcept;
    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;
    GpuMesh(GpuMesh&& other) noexcept;
    GpuMesh& operator=(GpuMesh&& other) noexcept;

    // 调用前释放旧句柄；失败后的部分初始化由 release 或析构清理。
    [[nodiscard]] bool create(const MeshRenderer& renderer);
    [[nodiscard]] bool update_deformation(const MeshRenderer& renderer);
    void bind_vertices() const;
    void release() noexcept;

    std::uint64_t node_id = 0;
    std::uint64_t instance_id = 0;
    std::uint64_t revision = 0;
    std::uint64_t deformation_revision = 0;
    bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle deformed_vertices = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
    std::array<float, 3> source_minimum{};
    std::array<float, 3> source_maximum{};

private:
    [[nodiscard]] static bgfx::VertexLayout vertex_layout();
    [[nodiscard]] bool pack_vertices(const schema::MeshDataT& data,
                                     std::span<const schema::MeshVertex> positions,
                                     std::span<const schema::Vec4> tangents,
                                     std::vector<GpuVertex>& packed);
};

} // namespace engine::render_detail
