#include "runtime/utils/mesh_utils.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace engine
{
namespace
{

using Vector = std::array<double, 3>;

bool float_value(double value) noexcept
{
    return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max();
}

bool unit_value(double value) noexcept
{
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

double dot(const Vector& a, const Vector& b) noexcept
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vector cross(const Vector& a, const Vector& b) noexcept
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

bool normalize(Vector& value) noexcept
{
    const double length = std::hypot(value[0], value[1], value[2]);
    if (!std::isfinite(length) || length <= 0.0)
    {
        return false;
    }
    for (double& item : value)
    {
        item /= length;
    }
    return true;
}

Vector orthogonal(const Vector& normal) noexcept
{
    const Vector axis = std::abs(normal[2]) < 0.9 ? Vector{0.0, 0.0, 1.0} : Vector{0.0, 1.0, 0.0};
    Vector tangent = cross(axis, normal);
    const bool normalized = normalize(tangent);
    (void)normalized;
    return tangent;
}

} // namespace

bool MeshUtils::validate(const schema::MeshDataT& data) noexcept
{
    const auto count = data.vertices.size();
    if (data.schema_version != 1 || count == 0 ||
        count > std::numeric_limits<std::uint32_t>::max() / (16 * sizeof(float)) ||
        data.indices.empty() || data.indices.size() % 3 != 0 ||
        data.indices.size() > std::numeric_limits<std::uint32_t>::max() / sizeof(std::uint32_t) ||
        (!data.texcoords.empty() && data.texcoords.size() != count) ||
        (!data.lightmap_texcoords.empty() && data.lightmap_texcoords.size() != count) ||
        (!data.tangents.empty() && data.tangents.size() != count) ||
        (!data.colors.empty() && data.colors.size() != count))
    {
        return false;
    }
    for (const auto& vertex : data.vertices)
    {
        if (!float_value(vertex.x()) || !float_value(vertex.y()) || !float_value(vertex.z()) ||
            !float_value(vertex.nx()) || !float_value(vertex.ny()) || !float_value(vertex.nz()) ||
            std::hypot(static_cast<double>(vertex.nx()), static_cast<double>(vertex.ny()),
                       static_cast<double>(vertex.nz())) <= 0.0)
        {
            return false;
        }
    }
    for (auto index : data.indices)
    {
        if (index >= count)
        {
            return false;
        }
    }
    for (const auto& uv : data.texcoords)
    {
        if (!float_value(uv.x()) || !float_value(uv.y()))
        {
            return false;
        }
    }
    for (const auto& uv : data.lightmap_texcoords)
    {
        if (!unit_value(uv.x()) || !unit_value(uv.y()))
        {
            return false;
        }
    }
    for (const auto& tangent : data.tangents)
    {
        if (!float_value(tangent.x()) || !float_value(tangent.y()) || !float_value(tangent.z()) ||
            (tangent.w() != -1.0 && tangent.w() != 1.0) ||
            std::hypot(tangent.x(), tangent.y(), tangent.z()) <= 0.0)
        {
            return false;
        }
    }
    for (const auto& color : data.colors)
    {
        if (!unit_value(color.r()) || !unit_value(color.g()) || !unit_value(color.b()) ||
            !unit_value(color.a()))
        {
            return false;
        }
    }
    std::size_t next_index = 0;
    for (const auto& submesh : data.submeshes)
    {
        if (submesh.first_index() != next_index || submesh.index_count() == 0 ||
            submesh.index_count() % 3 != 0 || submesh.material_slot() >= 16 ||
            submesh.index_count() > data.indices.size() - next_index)
        {
            return false;
        }
        next_index += submesh.index_count();
    }
    if (!data.submeshes.empty() && next_index != data.indices.size())
    {
        return false;
    }
    if (!data.lods.empty() && (!data.skin.empty() || !data.skeleton_guid.empty()))
    {
        return false;
    }
    std::size_t previous_count = data.indices.size();
    float previous_height = std::nextafter(1.0F, 2.0F);
    for (const auto& lod : data.lods)
    {
        if (!lod || !std::isfinite(lod->screen_height) || lod->screen_height <= 0 ||
            lod->screen_height > 1 || lod->screen_height >= previous_height ||
            lod->indices.empty() || lod->indices.size() % 3 != 0 ||
            lod->indices.size() >= previous_count)
        {
            return false;
        }
        for (const auto index : lod->indices)
        {
            if (index >= count)
            {
                return false;
            }
        }
        if (lod->submeshes.size() != std::max<std::size_t>(1, data.submeshes.size()))
        {
            return false;
        }
        next_index = 0;
        for (std::size_t part = 0; part < lod->submeshes.size(); ++part)
        {
            const auto& range = lod->submeshes[part];
            const auto slot = data.submeshes.empty() ? 0U : data.submeshes[part].material_slot();
            if (range.first_index() != next_index || range.index_count() == 0 ||
                range.index_count() % 3 != 0 || range.material_slot() != slot ||
                next_index > lod->indices.size() ||
                range.index_count() > lod->indices.size() - next_index)
            {
                return false;
            }
            next_index += range.index_count();
        }
        if (next_index != lod->indices.size())
        {
            return false;
        }
        previous_count = lod->indices.size();
        previous_height = lod->screen_height;
    }
    return true;
}

bool MeshUtils::build_tangents(const schema::MeshDataT& data, std::vector<schema::Vec4>& output)
{
    if (!validate(data))
    {
        output.clear();
        return false;
    }
    std::vector<Vector> tangents(data.vertices.size());
    std::vector<Vector> bitangents(data.vertices.size());
    // Adapted from bgfx examples/common/bgfx_utils.cpp calcTangents (BSD-2-Clause).
    // Copyright 2010-2025 Branimir Karadzic. See third_party/notices/bgfx.txt.
    // Changes: generated data, uint32 indices, double arithmetic and degenerate-UV fallback.
    if (data.tangents.empty() && !data.texcoords.empty())
    {
        for (std::size_t triangle = 0; triangle < data.indices.size(); triangle += 3)
        {
            const auto i0 = data.indices[triangle];
            const auto i1 = data.indices[triangle + 1];
            const auto i2 = data.indices[triangle + 2];
            const auto& a = data.vertices[i0];
            const auto& b = data.vertices[i1];
            const auto& c = data.vertices[i2];
            const Vector edge_b = {static_cast<double>(b.x()) - a.x(),
                                   static_cast<double>(b.y()) - a.y(),
                                   static_cast<double>(b.z()) - a.z()};
            const Vector edge_c = {static_cast<double>(c.x()) - a.x(),
                                   static_cast<double>(c.y()) - a.y(),
                                   static_cast<double>(c.z()) - a.z()};
            const double bu = data.texcoords[i1].x() - data.texcoords[i0].x();
            const double bv = data.texcoords[i1].y() - data.texcoords[i0].y();
            const double cu = data.texcoords[i2].x() - data.texcoords[i0].x();
            const double cv = data.texcoords[i2].y() - data.texcoords[i0].y();
            const double determinant = bu * cv - bv * cu;
            if (std::abs(determinant) <= 1.0e-20)
            {
                continue;
            }
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                const double tangent = (edge_b[axis] * cv - edge_c[axis] * bv) / determinant;
                const double bitangent = (edge_c[axis] * bu - edge_b[axis] * cu) / determinant;
                for (auto index : {i0, i1, i2})
                {
                    tangents[index][axis] += tangent;
                    bitangents[index][axis] += bitangent;
                }
            }
        }
    }
    std::vector<schema::Vec4> result;
    result.reserve(data.vertices.size());
    for (std::size_t index = 0; index < data.vertices.size(); ++index)
    {
        const auto& vertex = data.vertices[index];
        Vector normal = {vertex.nx(), vertex.ny(), vertex.nz()};
        if (!normalize(normal))
        {
            output.clear();
            return false;
        }
        Vector tangent = tangents[index];
        double handedness = 1.0;
        if (!data.tangents.empty())
        {
            const auto& supplied = data.tangents[index];
            tangent = {supplied.x(), supplied.y(), supplied.z()};
            handedness = supplied.w();
        }
        const double normal_dot_tangent = dot(normal, tangent);
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            tangent[axis] -= normal[axis] * normal_dot_tangent;
        }
        if (!normalize(tangent))
        {
            tangent = orthogonal(normal);
        }
        else if (data.tangents.empty())
        {
            handedness = dot(cross(normal, tangent), bitangents[index]) < 0.0 ? -1.0 : 1.0;
        }
        result.emplace_back(tangent[0], tangent[1], tangent[2], handedness);
    }
    output = std::move(result);
    return true;
}

} // namespace engine
