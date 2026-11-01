#include "runtime/render/resources/gpu_mesh.hpp"

#include "runtime/render/passes/skinning_pass.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/node.hpp"

#include <utility>
#include <vector>

namespace engine::render_detail
{

GpuMesh::~GpuMesh() noexcept
{
    release();
}

GpuMesh::GpuMesh(GpuMesh&& other) noexcept
{
    *this = std::move(other);
}

GpuMesh& GpuMesh::operator=(GpuMesh&& other) noexcept
{
    if (this != &other)
    {
        release();
        node_id = std::exchange(other.node_id, 0);
        instance_id = std::exchange(other.instance_id, 0);
        revision = std::exchange(other.revision, 0);
        deformation_revision = std::exchange(other.deformation_revision, 0);
        geometry = std::move(other.geometry);
        deformed_vertices = std::exchange(other.deformed_vertices,
                                          bgfx::DynamicVertexBufferHandle{bgfx::kInvalidHandle});
        palette =
            std::exchange(other.palette, bgfx::DynamicVertexBufferHandle{bgfx::kInvalidHandle});
        minimum = other.minimum;
        maximum = other.maximum;
        skinned_ = std::exchange(other.skinned_, false);
        joint_count_ = std::exchange(other.joint_count_, 0);
    }
    return *this;
}

bool GpuMesh::create(const MeshRenderer& renderer, std::shared_ptr<GpuGeometry> source)
{
    release();
    if (source == nullptr || !bgfx::isValid(source->vertices) || !bgfx::isValid(source->indices))
    {
        return false;
    }
    geometry = std::move(source);
    node_id = renderer.node().id();
    instance_id = renderer.mesh_instance_id();
    revision = renderer.mesh_revision();
    minimum = geometry->minimum;
    maximum = geometry->maximum;
    return true;
}

bool GpuMesh::update_deformation(const MeshRenderer& renderer, SkinningPass& skinning)
{
    if (geometry == nullptr)
    {
        return false;
    }
    const auto positions = renderer.deformed_vertices();
    const auto transforms = renderer.skinning_palette();
    const bool has_skin = !transforms.empty();
    if (deformation_revision == renderer.deformation_revision() && skinned_ == has_skin &&
        bgfx::isValid(deformed_vertices) == (has_skin || !positions.empty()))
    {
        return true;
    }
    if (has_skin)
    {
        const auto normal_transforms = renderer.skinning_normal_palette();
        std::array<float, 3> low;
        std::array<float, 3> high;
        if (transforms.size() != normal_transforms.size() || !skinning.prepare() ||
            !geometry->skinning_bounds(transforms, low, high))
        {
            return false;
        }
        if (!skinned_ || joint_count_ != transforms.size())
        {
            release_deformation();
            skinned_ = true;
            joint_count_ = static_cast<std::uint32_t>(transforms.size());
        }
        std::vector<std::array<float, 16>> matrices(transforms.begin(), transforms.end());
        matrices.insert(matrices.end(), normal_transforms.begin(), normal_transforms.end());
        const auto* memory = bgfx::copy(
            matrices.data(), static_cast<std::uint32_t>(matrices.size() * sizeof(matrices[0])));
        if (bgfx::isValid(palette))
        {
            bgfx::update(palette, 0, memory);
        }
        else
        {
            palette = bgfx::createDynamicVertexBuffer(memory, GpuGeometry::vector_layout(),
                                                      BGFX_BUFFER_COMPUTE_READ);
        }
        if (!bgfx::isValid(deformed_vertices))
        {
            deformed_vertices = bgfx::createDynamicVertexBuffer(
                geometry->vertex_count, GpuGeometry::vertex_layout(), BGFX_BUFFER_COMPUTE_WRITE);
        }
        if (!skinning.dispatch(*geometry, palette, deformed_vertices, joint_count_))
        {
            return false;
        }
        minimum = low;
        maximum = high;
    }
    else if (positions.empty())
    {
        release_deformation();
        minimum = geometry->minimum;
        maximum = geometry->maximum;
    }
    else
    {
        const auto* data = renderer.mesh_data();
        std::vector<GpuVertex> packed;
        if (data == nullptr ||
            !GpuGeometry::pack_vertices(*data, positions, renderer.deformed_tangents(), packed,
                                        minimum, maximum))
        {
            return false;
        }
        if (skinned_)
        {
            release_deformation();
        }
        // bgfx examples/35-dynamic: keep topology and update only the vertex stream.
        // Copyright 2011-2025 Branimir Karadzic; third_party/notices/bgfx.txt.
        const auto* memory = bgfx::copy(
            packed.data(), static_cast<std::uint32_t>(packed.size() * sizeof(GpuVertex)));
        if (bgfx::isValid(deformed_vertices))
        {
            bgfx::update(deformed_vertices, 0, memory);
        }
        else
        {
            deformed_vertices =
                bgfx::createDynamicVertexBuffer(memory, GpuGeometry::vertex_layout());
            if (!bgfx::isValid(deformed_vertices))
            {
                return false;
            }
        }
    }
    deformation_revision = renderer.deformation_revision();
    return true;
}

void GpuMesh::bind_vertices() const
{
    if (bgfx::isValid(deformed_vertices))
    {
        bgfx::setVertexBuffer(0, deformed_vertices);
    }
    else
    {
        bgfx::setVertexBuffer(0, geometry->vertices);
    }
}

void GpuMesh::release_deformation() noexcept
{
    for (auto* handle : {&deformed_vertices, &palette})
    {
        if (bgfx::isValid(*handle))
        {
            bgfx::destroy(*handle);
            *handle = BGFX_INVALID_HANDLE;
        }
    }
    skinned_ = false;
    joint_count_ = 0;
    deformation_revision = 0;
}

void GpuMesh::release() noexcept
{
    release_deformation();
    geometry.reset();
    node_id = 0;
    instance_id = 0;
    revision = 0;
}

} // namespace engine::render_detail
