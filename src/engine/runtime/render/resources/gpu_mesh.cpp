#include "runtime/render/resources/gpu_mesh.hpp"

#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/utils/mesh_utils.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace engine::render_detail
{

GpuMesh::~GpuMesh() noexcept
{
    release();
}

GpuMesh::GpuMesh(GpuMesh&& other) noexcept
    : node_id(std::exchange(other.node_id, 0)), instance_id(std::exchange(other.instance_id, 0)),
      revision(std::exchange(other.revision, 0)),
      deformation_revision(std::exchange(other.deformation_revision, 0)),
      vertices(std::exchange(other.vertices, bgfx::VertexBufferHandle{bgfx::kInvalidHandle})),
      deformed_vertices(std::exchange(other.deformed_vertices,
                                      bgfx::DynamicVertexBufferHandle{bgfx::kInvalidHandle})),
      indices(std::exchange(other.indices, bgfx::IndexBufferHandle{bgfx::kInvalidHandle})),
      minimum(other.minimum), maximum(other.maximum), source_minimum(other.source_minimum),
      source_maximum(other.source_maximum)
{
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
        vertices = std::exchange(other.vertices, bgfx::VertexBufferHandle{bgfx::kInvalidHandle});
        deformed_vertices = std::exchange(other.deformed_vertices,
                                          bgfx::DynamicVertexBufferHandle{bgfx::kInvalidHandle});
        indices = std::exchange(other.indices, bgfx::IndexBufferHandle{bgfx::kInvalidHandle});
        minimum = other.minimum;
        maximum = other.maximum;
        source_minimum = other.source_minimum;
        source_maximum = other.source_maximum;
    }
    return *this;
}

bgfx::VertexLayout GpuMesh::vertex_layout()
{
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .end();
    return layout;
}

bool GpuMesh::pack_vertices(const schema::MeshDataT& data,
                            std::span<const schema::MeshVertex> positions,
                            std::span<const schema::Vec4> tangents, std::vector<GpuVertex>& packed)
{
    if (positions.size() != data.vertices.size() || tangents.size() != positions.size())
    {
        return false;
    }
    packed.resize(positions.size());
    minimum.fill(std::numeric_limits<float>::max());
    maximum.fill(std::numeric_limits<float>::lowest());
    for (std::size_t index = 0; index < packed.size(); ++index)
    {
        const auto& source = positions[index];
        const auto& tangent = tangents[index];
        const auto uv = data.texcoords.empty() ? schema::Vec2{} : data.texcoords[index];
        const auto color = data.colors.empty() ? schema::Color{1, 1, 1, 1} : data.colors[index];
        const double normal_length =
            std::hypot(static_cast<double>(source.nx()), static_cast<double>(source.ny()),
                       static_cast<double>(source.nz()));
        if (!(normal_length > 0.0))
        {
            return false;
        }
        const std::array<double, 3> normal = {
            source.nx() / normal_length, source.ny() / normal_length, source.nz() / normal_length};
        const std::array<double, 3> bitangent = {normal[1] * tangent.z() - normal[2] * tangent.y(),
                                                 normal[2] * tangent.x() - normal[0] * tangent.z(),
                                                 normal[0] * tangent.y() - normal[1] * tangent.x()};
        const std::array<double, 3> orthogonal = {
            bitangent[1] * normal[2] - bitangent[2] * normal[1],
            bitangent[2] * normal[0] - bitangent[0] * normal[2],
            bitangent[0] * normal[1] - bitangent[1] * normal[0]};
        const double tangent_length = std::hypot(orthogonal[0], orthogonal[1], orthogonal[2]);
        if (!(tangent_length > 0.0))
        {
            return false;
        }
        packed[index] = {{source.x(), source.y(), source.z()},
                         {static_cast<float>(normal[0]), static_cast<float>(normal[1]),
                          static_cast<float>(normal[2])},
                         {static_cast<float>(orthogonal[0] / tangent_length),
                          static_cast<float>(orthogonal[1] / tangent_length),
                          static_cast<float>(orthogonal[2] / tangent_length),
                          static_cast<float>(tangent.w())},
                         {static_cast<float>(uv.x()), static_cast<float>(uv.y())},
                         {color.r(), color.g(), color.b(), color.a()}};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            minimum[axis] = std::min(minimum[axis], packed[index].position[axis]);
            maximum[axis] = std::max(maximum[axis], packed[index].position[axis]);
        }
    }
    return true;
}

bool GpuMesh::create(const MeshRenderer& renderer)
{
    const auto* data = renderer.mesh_data();
    std::vector<schema::Vec4> tangents;
    std::vector<GpuVertex> packed;
    if (data == nullptr || !MeshUtils::build_tangents(*data, tangents) ||
        !pack_vertices(*data, data->vertices, tangents, packed))
    {
        return false;
    }
    node_id = renderer.node().id();
    instance_id = renderer.mesh_instance_id();
    revision = renderer.mesh_revision();
    source_minimum = minimum;
    source_maximum = maximum;
    const auto vertex_bytes = packed.size() * sizeof(GpuVertex);
    const auto index_bytes = data->indices.size() * sizeof(std::uint32_t);
    vertices = bgfx::createVertexBuffer(
        bgfx::copy(packed.data(), static_cast<std::uint32_t>(vertex_bytes)), vertex_layout());
    indices = bgfx::createIndexBuffer(
        bgfx::copy(data->indices.data(), static_cast<std::uint32_t>(index_bytes)),
        BGFX_BUFFER_INDEX32);
    return bgfx::isValid(vertices) && bgfx::isValid(indices);
}

bool GpuMesh::update_deformation(const MeshRenderer& renderer)
{
    const auto positions = renderer.deformed_vertices();
    if (deformation_revision == renderer.deformation_revision() &&
        bgfx::isValid(deformed_vertices) == !positions.empty())
    {
        return true;
    }
    if (positions.empty())
    {
        if (bgfx::isValid(deformed_vertices))
        {
            bgfx::destroy(deformed_vertices);
            deformed_vertices = BGFX_INVALID_HANDLE;
        }
        minimum = source_minimum;
        maximum = source_maximum;
    }
    else
    {
        const auto* data = renderer.mesh_data();
        std::vector<GpuVertex> packed;
        if (data == nullptr ||
            !pack_vertices(*data, positions, renderer.deformed_tangents(), packed))
        {
            return false;
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
            deformed_vertices = bgfx::createDynamicVertexBuffer(memory, vertex_layout());
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
        bgfx::setVertexBuffer(0, vertices);
    }
}

void GpuMesh::release() noexcept
{
    if (bgfx::isValid(vertices))
    {
        bgfx::destroy(vertices);
    }
    if (bgfx::isValid(deformed_vertices))
    {
        bgfx::destroy(deformed_vertices);
    }
    if (bgfx::isValid(indices))
    {
        bgfx::destroy(indices);
    }
    vertices = BGFX_INVALID_HANDLE;
    deformed_vertices = BGFX_INVALID_HANDLE;
    indices = BGFX_INVALID_HANDLE;
    deformation_revision = 0;
}

} // namespace engine::render_detail
