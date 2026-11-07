#pragma once

#include "runtime/resource/resource_reference.hpp"
#include "runtime/resource/types/texture_resource.hpp"

#include "runtime/render/resources/gpu_vertex.hpp"
#include "runtime/schema/mesh_generated.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace engine
{
class MeshRenderer;
class Resource;
} // namespace engine

namespace engine::render_detail
{

// 不可变源几何由绘制实例共享；资源强引用区分相同 GUID 的不同发布版本。
struct GpuGeometry
{
    GpuGeometry() = default;
    ~GpuGeometry() noexcept;
    GpuGeometry(const GpuGeometry&) = delete;
    GpuGeometry& operator=(const GpuGeometry&) = delete;

    [[nodiscard]] bool create(const MeshRenderer& renderer);
    [[nodiscard]] bool skinning_bounds(std::span<const std::array<float, 16>> palette,
                                       std::array<float, 3>& minimum,
                                       std::array<float, 3>& maximum) const noexcept;
    [[nodiscard]] std::uint32_t resource_count() const noexcept;
    [[nodiscard]] bgfx::IndexBufferHandle index_buffer(std::size_t level = 0) const noexcept;
    [[nodiscard]] std::uint32_t index_count(std::size_t level = 0) const noexcept;
    [[nodiscard]] std::span<const schema::MeshSubmesh>
    submeshes(std::size_t level = 0) const noexcept;
    [[nodiscard]] std::size_t lod_count() const noexcept;
    void release() noexcept;
    [[nodiscard]] static bgfx::VertexLayout vertex_layout();
    [[nodiscard]] static bgfx::VertexLayout vector_layout();
    [[nodiscard]] static bool
    pack_vertices(const schema::MeshDataT& data, std::span<const schema::MeshVertex> positions,
                  std::span<const schema::Vec4> tangents, std::vector<GpuVertex>& packed,
                  std::array<float, 3>& minimum, std::array<float, 3>& maximum);

    ResourceReference resource;
    bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle influences = BGFX_INVALID_HANDLE;
    std::uint32_t vertex_count = 0;
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};

private:
    struct Lod
    {
        bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
        std::uint32_t index_count = 0;
        std::vector<schema::MeshSubmesh> submeshes;
    };
    std::vector<Lod> lods_;
    std::vector<schema::MeshSubmesh> submeshes_;
    std::uint32_t index_count_ = 0;
    struct JointBounds
    {
        std::array<float, 3> minimum{};
        std::array<float, 3> maximum{};
        bool used = false;
    };
    std::vector<JointBounds> joint_bounds_;
};

} // namespace engine::render_detail
