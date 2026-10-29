#include "plugins/editor/modeling/mesh_geometry.hpp"

#include "plugins/editor/modeling/mesh_geometry_internal.hpp"

#include <xatlas.h>

#include <array>
#include <memory>

namespace engine::plugins::modeling
{
namespace
{

namespace math = detail::geometry;
using math::Point;

struct AtlasCorner
{
    std::uint64_t vertex = 0;
    schema::Vec2 uv;
    schema::Color color = {1.0F, 1.0F, 1.0F, 1.0F};
    schema::Vec2* destination = nullptr;
};

struct AtlasEdge
{
    std::size_t face = 0;
    std::uint32_t first = 0;
    std::uint32_t second = 0;
};

struct AtlasTriangle
{
    std::size_t face = 0;
    std::array<std::uint32_t, 3> corners{};
    std::array<schema::Vec2, 3> texcoords{};
};

bool same_uv(const schema::Vec2& a, const schema::Vec2& b)
{
    return std::abs(a.x() - b.x()) <= 0.000001 && std::abs(a.y() - b.y()) <= 0.000001;
}

bool repair_unused_corners(const schema::EditableMeshDataT& data,
                           const std::vector<AtlasCorner>& corners,
                           const std::vector<std::vector<std::uint32_t>>& loops,
                           std::vector<bool>& assigned, std::vector<schema::Vec2>& texcoords,
                           std::string& error)
{
    for (const auto& loop : loops)
    {
        for (std::size_t i = 0; i < loop.size(); ++i)
        {
            const auto index = loop[i];
            if (assigned[index])
            {
                continue;
            }
            std::size_t before = (i + loop.size() - 1) % loop.size();
            std::size_t after = (i + 1) % loop.size();
            while (before != i && !assigned[loop[before]])
            {
                before = (before + loop.size() - 1) % loop.size();
            }
            while (after != i && !assigned[loop[after]])
            {
                after = (after + 1) % loop.size();
            }
            if (before == i || after == i || before == after)
            {
                error = "UV atlas did not return enough corners for a polygon loop";
                return false;
            }
            const Point a = math::position(data, corners[loop[before]].vertex);
            const Point b = math::position(data, corners[loop[after]].vertex);
            const Point p = math::position(data, corners[index].vertex);
            const Point edge = b - a;
            const double size = math::dot(edge, edge);
            if (size <= math::epsilon * math::epsilon ||
                math::length(math::cross(p - a, edge)) > size * 0.000001)
            {
                error = "UV atlas omitted a non-collinear polygon corner";
                return false;
            }
            const double t = math::dot(p - a, edge) / size;
            const auto& uv0 = texcoords[loop[before]];
            const auto& uv1 = texcoords[loop[after]];
            texcoords[index] =
                schema::Vec2(uv0.x() + (uv1.x() - uv0.x()) * t, uv0.y() + (uv1.y() - uv0.y()) * t);
            assigned[index] = true;
        }
    }
    return true;
}

} // namespace

bool MeshGeometry::unwrap_uv(schema::EditableMeshDataT& data,
                             std::span<const std::uint64_t> face_ids,
                             const MeshUvUnwrapOptions& options, std::string& error)
{
    error.clear();
    if (!std::isfinite(options.padding) || options.padding < 0.0 || options.padding >= 0.25 ||
        options.resolution < 64 || options.resolution > 8192 || options.iterations == 0 ||
        options.iterations > 16)
    {
        error = "Atlas requires resolution 64..8192, iterations 1..16, and padding in [0, 0.25)";
        return false;
    }
    schema::EditableMeshDataT candidate = data;
    std::vector<schema::ModelingFaceT*> faces;
    if (!math::selected_faces(candidate, face_ids, faces, error))
    {
        return false;
    }
    std::vector<AtlasCorner> corners;
    std::vector<AtlasTriangle> triangles;
    std::vector<std::vector<std::uint32_t>> loops;
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 2>> input_uv;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint32_t> materials;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<AtlasEdge>> edges;
    for (std::size_t f = 0; f < faces.size(); ++f)
    {
        auto& face = *faces[f];
        const auto base = static_cast<std::uint32_t>(corners.size());
        const auto append_loop = [&](const auto& vertices, auto& texcoords, const auto& colors)
        {
            const auto first = static_cast<std::uint32_t>(corners.size());
            texcoords.resize(vertices.size());
            std::vector<std::uint32_t> loop;
            for (std::size_t i = 0; i < vertices.size(); ++i)
            {
                AtlasCorner corner;
                corner.vertex = vertices[i];
                corner.uv = texcoords[i];
                corner.destination = &texcoords[i];
                if (!colors.empty())
                {
                    corner.color = colors[i];
                }
                const auto current = static_cast<std::uint32_t>(corners.size());
                corners.push_back(corner);
                const Point p = math::position(candidate, vertices[i]);
                positions.push_back(
                    {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
                input_uv.push_back(
                    {static_cast<float>(corner.uv.x()), static_cast<float>(corner.uv.y())});
                loop.push_back(current);
                const auto next = first + static_cast<std::uint32_t>((i + 1) % vertices.size());
                edges[math::edge_key(vertices[i], vertices[(i + 1) % vertices.size()])].push_back(
                    {f, current, next});
            }
            loops.push_back(std::move(loop));
        };
        append_loop(face.vertices, face.texcoords, face.colors);
        for (auto& hole : face.holes)
        {
            append_loop(hole->vertices, hole->texcoords, hole->colors);
        }
        std::vector<std::uint32_t> local;
        if (!EditableMesh::triangulate_face(candidate, face, local, error))
        {
            return false;
        }
        for (std::size_t i = 0; i < local.size(); i += 3)
        {
            AtlasTriangle triangle;
            triangle.face = f;
            for (std::size_t j = 0; j < 3; ++j)
            {
                triangle.corners[j] = base + local[i + j];
                indices.push_back(triangle.corners[j]);
            }
            triangles.push_back(triangle);
        }
    }
    if (positions.size() > 1000000 || indices.size() > 6000000 || indices.empty())
    {
        error = "Atlas input exceeds the modeling resource limit or has no triangles";
        return false;
    }
    std::set<std::pair<std::uint64_t, std::uint64_t>> seams;
    for (const auto& edge : options.seams)
    {
        const auto key = math::edge_key(edge.first, edge.second);
        if (!edges.contains(key) || !seams.insert(key).second)
        {
            error = "UV seam selection contains a missing or duplicate edge";
            return false;
        }
    }
    if (options.respect_existing_seams)
    {
        for (const auto& [key, sides] : edges)
        {
            if (sides.size() != 2)
            {
                continue;
            }
            const auto& a = sides[0];
            const auto& b = sides[1];
            const bool reversed = corners[a.first].vertex == corners[b.second].vertex;
            const auto b_first = reversed ? b.second : b.first;
            const auto b_second = reversed ? b.first : b.second;
            if (!same_uv(corners[a.first].uv, corners[b_first].uv) ||
                !same_uv(corners[a.second].uv, corners[b_second].uv))
            {
                seams.insert(key);
            }
        }
    }
    // xatlas 的 material 是图表边界约束。仅在显式 seam 两侧分配不同分组；
    // 自动展开仍可增添必要接缝，且不能重新连接调用方要求保留的接缝。
    std::vector<std::vector<std::size_t>> conflicts(faces.size());
    for (const auto& key : seams)
    {
        const auto& sides = edges.at(key);
        for (std::size_t i = 0; i < sides.size(); ++i)
        {
            for (std::size_t j = i + 1; j < sides.size(); ++j)
            {
                conflicts[sides[i].face].push_back(sides[j].face);
                conflicts[sides[j].face].push_back(sides[i].face);
            }
        }
    }
    std::vector<std::uint32_t> colors(faces.size(), std::numeric_limits<std::uint32_t>::max());
    for (std::size_t i = 0; i < faces.size(); ++i)
    {
        std::set<std::uint32_t> unavailable;
        for (const auto other : conflicts[i])
        {
            unavailable.insert(colors[other]);
        }
        colors[i] = 0;
        while (unavailable.contains(colors[i]))
        {
            ++colors[i];
        }
    }
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> partitions;
    for (const auto& triangle : triangles)
    {
        const auto key = std::make_pair(faces[triangle.face]->material_slot, colors[triangle.face]);
        const auto [entry, inserted] =
            partitions.emplace(key, static_cast<std::uint32_t>(partitions.size()));
        (void)inserted;
        materials.push_back(entry->second);
    }
    using AtlasOwner = std::unique_ptr<xatlas::Atlas, decltype(&xatlas::Destroy)>;
    AtlasOwner atlas(xatlas::Create(), &xatlas::Destroy);
    if (!atlas)
    {
        error = "Cannot allocate the UV atlas";
        return false;
    }
    xatlas::MeshDecl declaration;
    declaration.vertexCount = static_cast<std::uint32_t>(positions.size());
    declaration.vertexPositionData = positions.data();
    declaration.vertexPositionStride = sizeof(positions.front());
    if (options.respect_existing_seams)
    {
        declaration.vertexUvData = input_uv.data();
        declaration.vertexUvStride = sizeof(input_uv.front());
    }
    declaration.indexCount = static_cast<std::uint32_t>(indices.size());
    declaration.indexData = indices.data();
    declaration.indexFormat = xatlas::IndexFormat::UInt32;
    declaration.faceMaterialData = materials.data();
    const auto status = xatlas::AddMesh(atlas.get(), declaration, 1);
    if (status != xatlas::AddMeshError::Success)
    {
        error = "UV atlas rejected the mesh: " + std::string(xatlas::StringForEnum(status));
        return false;
    }
    xatlas::ChartOptions chart;
    chart.maxIterations = options.iterations;
    chart.fixWinding = true;
    xatlas::PackOptions pack;
    pack.resolution = options.resolution;
    pack.padding = static_cast<std::uint32_t>(std::ceil(options.padding * options.resolution));
    pack.rotateCharts = options.rotate;
    pack.rotateChartsToAxis = options.rotate;
    xatlas::Generate(atlas.get(), chart, pack);
    if (atlas->atlasCount > 1)
    {
        // 单材质 UV 表达一个图集。分辨率是密度提示，必要时扩展单张尺寸而非丢弃其他页。
        pack.resolution = 0;
        pack.texelsPerUnit = atlas->texelsPerUnit;
        xatlas::PackCharts(atlas.get(), pack);
    }
    if (atlas->meshCount != 1 || atlas->width == 0 || atlas->height == 0 ||
        atlas->atlasCount != 1 || atlas->meshes[0].indexCount != indices.size())
    {
        error = "UV atlas did not produce one complete packed mesh";
        return false;
    }
    const auto& result = atlas->meshes[0];
    for (std::uint32_t i = 0; i < result.chartCount; ++i)
    {
        if (result.chartArray[i].type == xatlas::ChartType::Invalid)
        {
            error = "UV atlas produced an invalid chart";
            return false;
        }
    }
    std::vector<bool> assigned(corners.size(), false);
    std::vector<bool> split(faces.size(), false);
    std::vector<schema::Vec2> texcoords(corners.size());
    for (std::size_t i = 0; i < indices.size(); ++i)
    {
        const auto& vertex = result.vertexArray[result.indexArray[i]];
        if (vertex.xref != indices[i] || vertex.atlasIndex != 0 || !std::isfinite(vertex.uv[0]) ||
            !std::isfinite(vertex.uv[1]))
        {
            error = "UV atlas returned an incomplete or reordered corner mapping";
            return false;
        }
        const schema::Vec2 uv(vertex.uv[0] / static_cast<double>(atlas->width),
                              vertex.uv[1] / static_cast<double>(atlas->height));
        auto& triangle = triangles[i / 3];
        triangle.texcoords[i % 3] = uv;
        const auto corner = indices[i];
        if (assigned[corner] && !same_uv(texcoords[corner], uv))
        {
            split[triangle.face] = true;
        }
        assigned[corner] = true;
        texcoords[corner] = uv;
    }
    if (!repair_unused_corners(candidate, corners, loops, assigned, texcoords, error))
    {
        return false;
    }
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        *corners[i].destination = texcoords[i];
    }
    std::set<std::uint64_t> replaced;
    std::vector<std::unique_ptr<schema::ModelingFaceT>> replacements;
    for (const auto& triangle : triangles)
    {
        if (!split[triangle.face])
        {
            continue;
        }
        const auto& source = *faces[triangle.face];
        replaced.insert(source.id);
        auto face = std::make_unique<schema::ModelingFaceT>();
        face->id = candidate.next_id++;
        face->material_slot = source.material_slot;
        face->smoothing_group = source.smoothing_group;
        for (std::size_t i = 0; i < 3; ++i)
        {
            face->vertices.push_back(corners[triangle.corners[i]].vertex);
            face->colors.push_back(corners[triangle.corners[i]].color);
            face->texcoords.push_back(triangle.texcoords[i]);
        }
        replacements.push_back(std::move(face));
    }
    std::erase_if(candidate.faces, [&](const auto& face) { return replaced.contains(face->id); });
    for (auto& face : replacements)
    {
        candidate.faces.push_back(std::move(face));
    }
    return math::commit(data, std::move(candidate), error);
}

} // namespace engine::plugins::modeling
