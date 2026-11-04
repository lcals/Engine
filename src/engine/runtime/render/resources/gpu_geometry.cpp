#include "runtime/render/resources/gpu_geometry.hpp"

#include "runtime/performance/profiling.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/utils/mesh_utils.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::render_detail
{

GpuGeometry::~GpuGeometry() noexcept
{
    release();
}

bgfx::VertexLayout GpuGeometry::vertex_layout()
{
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .skip(2 * sizeof(float))
        .end();
    return layout;
}

bool GpuGeometry::pack_vertices(const schema::MeshDataT& data,
                                std::span<const schema::MeshVertex> positions,
                                std::span<const schema::Vec4> tangents,
                                std::vector<GpuVertex>& packed, std::array<float, 3>& minimum,
                                std::array<float, 3>& maximum)
{
    ENGINE_PROFILE_ZONE("Render.geometryPackVertices");
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
        const auto lightmap_uv =
            data.lightmap_texcoords.empty() ? schema::Vec2{} : data.lightmap_texcoords[index];
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
                         {color.r(), color.g(), color.b(), color.a()},
                         {static_cast<float>(lightmap_uv.x()), static_cast<float>(lightmap_uv.y())},
                         {}};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            minimum[axis] = std::min(minimum[axis], packed[index].position[axis]);
            maximum[axis] = std::max(maximum[axis], packed[index].position[axis]);
        }
    }
    return true;
}

bgfx::VertexLayout GpuGeometry::vector_layout()
{
    bgfx::VertexLayout layout;
    layout.begin().add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float).end();
    return layout;
}

bool GpuGeometry::create(const MeshRenderer& renderer)
{
    ENGINE_PROFILE_ZONE("Render.geometryCreate");
    release();
    const auto* data = renderer.mesh_data();
    std::vector<schema::Vec4> tangents;
    std::vector<GpuVertex> packed;
    if (data == nullptr || !MeshUtils::build_tangents(*data, tangents) ||
        !pack_vertices(*data, data->vertices, tangents, packed, minimum, maximum))
    {
        return false;
    }
    const auto vertex_bytes = packed.size() * sizeof(GpuVertex);
    const auto index_bytes = data->indices.size() * sizeof(std::uint32_t);
    if (vertex_bytes > std::numeric_limits<std::uint32_t>::max() ||
        index_bytes > std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }
    std::vector<std::array<float, 4>> skin;
    if (!data->skin.empty())
    {
        if (data->skin.size() != packed.size())
        {
            return false;
        }
        std::size_t count = 0;
        for (const auto& influence : data->skin)
        {
            count = std::max(count, std::size_t{1} + std::max({influence.j0(), influence.j1(),
                                                               influence.j2(), influence.j3()}));
        }
        joint_bounds_.resize(count);
        skin.reserve(packed.size() * 2);
        for (std::size_t vertex = 0; vertex < packed.size(); ++vertex)
        {
            const auto& input = data->skin[vertex];
            const std::array<std::uint16_t, 4> joints = {input.j0(), input.j1(), input.j2(),
                                                         input.j3()};
            std::array<float, 4> weights = {input.w0(), input.w1(), input.w2(), input.w3()};
            const float sum = weights[0] + weights[1] + weights[2] + weights[3];
            if (!std::isfinite(sum) || std::abs(sum - 1.0F) > 0.0001F)
            {
                return false;
            }
            for (std::size_t item = 0; item < weights.size(); ++item)
            {
                if (!std::isfinite(weights[item]) || weights[item] < 0 || weights[item] > 1)
                {
                    return false;
                }
                weights[item] /= sum;
                if (weights[item] == 0)
                {
                    continue;
                }
                auto& bounds = joint_bounds_[joints[item]];
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    const auto value = packed[vertex].position[axis];
                    bounds.minimum[axis] =
                        bounds.used ? std::min(bounds.minimum[axis], value) : value;
                    bounds.maximum[axis] =
                        bounds.used ? std::max(bounds.maximum[axis], value) : value;
                }
                bounds.used = true;
            }
            skin.push_back({static_cast<float>(joints[0]), static_cast<float>(joints[1]),
                            static_cast<float>(joints[2]), static_cast<float>(joints[3])});
            skin.push_back(weights);
        }
        influences = bgfx::createVertexBuffer(
            bgfx::copy(skin.data(), static_cast<std::uint32_t>(skin.size() * sizeof(skin[0]))),
            vector_layout(), BGFX_BUFFER_COMPUTE_READ);
        if (!bgfx::isValid(influences))
        {
            return false;
        }
    }
    {
        // 这里只采集 CPU 拷贝与提交；GPU 执行时间由 GPU 时间戳表示。
        ENGINE_PROFILE_ZONE("Render.geometryBuffersSubmit");
        vertices = bgfx::createVertexBuffer(
            bgfx::copy(packed.data(), static_cast<std::uint32_t>(vertex_bytes)), vertex_layout(),
            BGFX_BUFFER_COMPUTE_READ);
        indices = bgfx::createIndexBuffer(
            bgfx::copy(data->indices.data(), static_cast<std::uint32_t>(index_bytes)),
            BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ);
    }
    if (!bgfx::isValid(vertices) || !bgfx::isValid(indices))
    {
        return false;
    }
    index_count_ = static_cast<std::uint32_t>(data->indices.size());
    submeshes_ = data->submeshes;
    for (const auto& level : data->lods)
    {
        Lod lod;
        lod.index_count = static_cast<std::uint32_t>(level->indices.size());
        lod.submeshes = level->submeshes;
        lod.indices = bgfx::createIndexBuffer(
            bgfx::copy(level->indices.data(), lod.index_count * sizeof(std::uint32_t)),
            BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ);
        if (!bgfx::isValid(lod.indices))
        {
            return false;
        }
        lods_.push_back(std::move(lod));
    }
    resource = renderer.mesh_resource();
    vertex_count = static_cast<std::uint32_t>(packed.size());
    return true;
}

bool GpuGeometry::skinning_bounds(std::span<const std::array<float, 16>> palette,
                                  std::array<float, 3>& output_minimum,
                                  std::array<float, 3>& output_maximum) const noexcept
{
    output_minimum = {};
    output_maximum = {};
    if (joint_bounds_.empty() || palette.size() < joint_bounds_.size())
    {
        return false;
    }
    std::array<double, 3> low;
    std::array<double, 3> high;
    low.fill(std::numeric_limits<double>::max());
    high.fill(std::numeric_limits<double>::lowest());
    for (std::size_t joint = 0; joint < joint_bounds_.size(); ++joint)
    {
        const auto& bounds = joint_bounds_[joint];
        if (!bounds.used)
        {
            continue;
        }
        const auto& matrix = palette[joint];
        for (unsigned int corner = 0; corner < 8; ++corner)
        {
            const double x = (corner & 1) ? bounds.maximum[0] : bounds.minimum[0];
            const double y = (corner & 2) ? bounds.maximum[1] : bounds.minimum[1];
            const double z = (corner & 4) ? bounds.maximum[2] : bounds.minimum[2];
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                const double value = matrix[axis] * x + matrix[axis + 4] * y +
                                     matrix[axis + 8] * z + matrix[axis + 12];
                if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                {
                    return false;
                }
                low[axis] = std::min(low[axis], value);
                high[axis] = std::max(high[axis], value);
            }
        }
    }
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        // 留出浮点混合误差，边界必须保守覆盖 GPU 计算结果。
        const double margin = 1.0e-5 * std::max({1.0, std::abs(low[axis]), std::abs(high[axis])});
        if (low[axis] - margin < -std::numeric_limits<float>::max() ||
            high[axis] + margin > std::numeric_limits<float>::max())
        {
            return false;
        }
        output_minimum[axis] = static_cast<float>(low[axis] - margin);
        output_maximum[axis] = static_cast<float>(high[axis] + margin);
    }
    return true;
}

std::uint32_t GpuGeometry::resource_count() const noexcept
{
    return static_cast<std::uint32_t>(bgfx::isValid(vertices)) +
           static_cast<std::uint32_t>(bgfx::isValid(indices)) +
           static_cast<std::uint32_t>(bgfx::isValid(influences)) +
           static_cast<std::uint32_t>(lods_.size());
}

bgfx::IndexBufferHandle GpuGeometry::index_buffer(std::size_t level) const noexcept
{
    return level == 0              ? indices
           : level <= lods_.size() ? lods_[level - 1].indices
                                   : bgfx::IndexBufferHandle{bgfx::kInvalidHandle};
}

std::uint32_t GpuGeometry::index_count(std::size_t level) const noexcept
{
    return level == 0 ? index_count_ : level <= lods_.size() ? lods_[level - 1].index_count : 0;
}

std::span<const schema::MeshSubmesh> GpuGeometry::submeshes(std::size_t level) const noexcept
{
    if (level == 0)
    {
        return submeshes_;
    }
    return level <= lods_.size() ? std::span<const schema::MeshSubmesh>(lods_[level - 1].submeshes)
                                 : std::span<const schema::MeshSubmesh>{};
}

std::size_t GpuGeometry::lod_count() const noexcept
{
    return lods_.size() + 1;
}

void GpuGeometry::release() noexcept
{
    for (auto& lod : lods_)
    {
        if (bgfx::isValid(lod.indices))
        {
            bgfx::destroy(lod.indices);
        }
    }
    lods_.clear();
    submeshes_.clear();
    index_count_ = 0;
    for (auto* handle : {&vertices, &influences})
    {
        if (bgfx::isValid(*handle))
        {
            bgfx::destroy(*handle);
            *handle = BGFX_INVALID_HANDLE;
        }
    }
    if (bgfx::isValid(indices))
    {
        bgfx::destroy(indices);
        indices = BGFX_INVALID_HANDLE;
    }
    resource.reset();
    joint_bounds_.clear();
    vertex_count = 0;
}

} // namespace engine::render_detail
