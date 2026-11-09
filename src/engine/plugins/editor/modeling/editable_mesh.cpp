#include "plugins/editor/modeling/editable_mesh.hpp"

#include "editor/mesh/mesh_tangents.hpp"
#include "plugins/editor/modeling/primitive_options.hpp"

#include "plugins/editor/modeling/modeling_math.hpp"
#include "runtime/utils/mesh_utils.hpp"

#include <flatbuffers/verifier.h>
#include <mapbox/earcut.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace engine::plugins::modeling
{
namespace
{

namespace math = detail::topology;
using math::ProjectedPoint;
using math::Vector;
using Ring = std::vector<ProjectedPoint>;
using VertexLookup = std::unordered_map<std::uint64_t, Vector>;

VertexLookup make_lookup(const schema::EditableMeshDataT& data)
{
    VertexLookup result;
    result.reserve(data.vertices.size());
    for (const auto& vertex : data.vertices)
    {
        result.emplace(vertex.id(), math::position(vertex));
    }
    return result;
}

Vector lookup_position(const VertexLookup& lookup, std::uint64_t id)
{
    const auto found = lookup.find(id);
    return found == lookup.end() ? Vector{} : found->second;
}

Vector lookup_normal(const VertexLookup& lookup, const schema::ModelingFaceT& face)
{
    Vector normal{};
    for (std::size_t i = 0; i < face.vertices.size(); ++i)
    {
        const auto a = lookup_position(lookup, face.vertices[i]);
        const auto b = lookup_position(lookup, face.vertices[(i + 1) % face.vertices.size()]);
        normal[0] += (a[1] - b[1]) * (a[2] + b[2]);
        normal[1] += (a[2] - b[2]) * (a[0] + b[0]);
        normal[2] += (a[0] - b[0]) * (a[1] + b[1]);
    }
    return normal;
}

bool fail(std::string& error, std::string message)
{
    error = std::move(message);
    return false;
}

double orientation(const ProjectedPoint& a, const ProjectedPoint& b, const ProjectedPoint& c)
{
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}

bool on_segment(const ProjectedPoint& a, const ProjectedPoint& b, const ProjectedPoint& point,
                double tolerance)
{
    return std::abs(orientation(a, b, point)) <= tolerance &&
           point[0] >= std::min(a[0], b[0]) - tolerance &&
           point[0] <= std::max(a[0], b[0]) + tolerance &&
           point[1] >= std::min(a[1], b[1]) - tolerance &&
           point[1] <= std::max(a[1], b[1]) + tolerance;
}

bool intersects(const ProjectedPoint& a, const ProjectedPoint& b, const ProjectedPoint& c,
                const ProjectedPoint& d, double tolerance)
{
    const auto ab_c = orientation(a, b, c);
    const auto ab_d = orientation(a, b, d);
    const auto cd_a = orientation(c, d, a);
    const auto cd_b = orientation(c, d, b);
    if (((ab_c > tolerance && ab_d < -tolerance) || (ab_c < -tolerance && ab_d > tolerance)) &&
        ((cd_a > tolerance && cd_b < -tolerance) || (cd_a < -tolerance && cd_b > tolerance)))
    {
        return true;
    }
    return on_segment(a, b, c, tolerance) || on_segment(a, b, d, tolerance) ||
           on_segment(c, d, a, tolerance) || on_segment(c, d, b, tolerance);
}

bool inside(const Ring& ring, const ProjectedPoint& point)
{
    bool result = false;
    for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++)
    {
        const auto& a = ring[i];
        const auto& b = ring[j];
        if ((a[1] > point[1]) != (b[1] > point[1]) &&
            point[0] < (b[0] - a[0]) * (point[1] - a[1]) / (b[1] - a[1]) + a[0])
        {
            result = !result;
        }
    }
    return result;
}

double signed_area(const Ring& ring)
{
    double result = 0.0;
    for (std::size_t i = 0; i < ring.size(); ++i)
    {
        const auto& a = ring[i];
        const auto& b = ring[(i + 1) % ring.size()];
        result += a[0] * b[1] - a[1] * b[0];
    }
    return result * 0.5;
}

bool rings_for_face(const VertexLookup& lookup, const schema::ModelingFaceT& face,
                    std::vector<Ring>& rings, std::vector<std::uint64_t>& vertices,
                    std::string& error)
{
    rings.clear();
    vertices.clear();
    if (face.vertices.size() < 3 || face.vertices.size() > 65536 || face.holes.size() > 1024)
    {
        return fail(error, "Face loops require 3 to 65536 vertices and at most 1024 holes");
    }
    const auto normal = lookup_normal(lookup, face);
    if (math::length(normal) <= math::epsilon)
    {
        return fail(error, "Polygon area is zero or winding is invalid");
    }
    const auto unit_normal = math::normalized(normal);
    const auto origin = lookup_position(lookup, face.vertices.front());
    const auto axis = math::projection_axis(normal);
    double scale = 1.0;
    for (auto id : face.vertices)
    {
        scale = std::max(scale, math::length(math::sub(lookup_position(lookup, id), origin)));
    }
    const auto tolerance = 1.0e-9 * scale * scale;
    std::unordered_set<std::uint64_t> used;
    const auto add_ring = [&](const auto& ids, const auto& uv, const auto& colors) -> bool
    {
        if (ids.size() < 3 || ids.size() > 65536 || (!uv.empty() && uv.size() != ids.size()) ||
            (!colors.empty() && colors.size() != ids.size()))
        {
            return fail(error, "Invalid face loop or corner attribute count");
        }
        Ring ring;
        for (auto id : ids)
        {
            const auto vertex = lookup.find(id);
            if (vertex == lookup.end() || !used.insert(id).second)
            {
                return fail(error, "Face references a missing or duplicate vertex");
            }
            const auto value = vertex->second;
            if (std::abs(math::dot(math::sub(value, origin), unit_normal)) > 1.0e-7 * scale)
            {
                return fail(
                    error,
                    "Polygon vertices are not coplanar; triangulate or split the face first");
            }
            ring.push_back(math::project(value, axis));
            vertices.push_back(id);
        }
        for (const auto& value : uv)
        {
            if (!std::isfinite(value.x()) || !std::isfinite(value.y()) ||
                std::abs(value.x()) > 1.0e12 || std::abs(value.y()) > 1.0e12)
            {
                return fail(error, "UV coordinates must be finite");
            }
        }
        for (const auto& value : colors)
        {
            for (auto item : {value.r(), value.g(), value.b(), value.a()})
            {
                if (!std::isfinite(item) || item < 0.0F || item > 1.0F)
                {
                    return fail(error, "Vertex colors must be between 0 and 1");
                }
            }
        }
        if (std::abs(signed_area(ring)) <= tolerance)
        {
            return fail(error, "Face loop area is zero");
        }
        for (std::size_t i = 0; i < ring.size(); ++i)
        {
            const auto next = (i + 1) % ring.size();
            if (std::hypot(ring[i][0] - ring[next][0], ring[i][1] - ring[next][1]) <= math::epsilon)
            {
                return fail(error, "Face contains a zero-length edge");
            }
            for (std::size_t j = i + 1; j < ring.size(); ++j)
            {
                if (j == next || (j + 1) % ring.size() == i)
                {
                    continue;
                }
                if (intersects(ring[i], ring[next], ring[j], ring[(j + 1) % ring.size()],
                               tolerance))
                {
                    return fail(error, "Face loop intersects itself");
                }
            }
        }
        rings.push_back(std::move(ring));
        return true;
    };
    if (!add_ring(face.vertices, face.texcoords, face.colors))
    {
        return false;
    }
    for (const auto& hole : face.holes)
    {
        if (!hole || !add_ring(hole->vertices, hole->texcoords, hole->colors))
        {
            return fail(error, "Invalid hole loop: " + error);
        }
    }
    for (std::size_t i = 1; i < rings.size(); ++i)
    {
        if (!inside(rings.front(), rings[i].front()))
        {
            return fail(error, "Holes must lie inside the outer loop");
        }
        for (std::size_t j = 0; j < i; ++j)
        {
            for (std::size_t a = 0; a < rings[i].size(); ++a)
            {
                for (std::size_t b = 0; b < rings[j].size(); ++b)
                {
                    if (intersects(rings[i][a], rings[i][(a + 1) % rings[i].size()], rings[j][b],
                                   rings[j][(b + 1) % rings[j].size()], tolerance))
                    {
                        return fail(error, "Hole intersects another face loop");
                    }
                }
            }
            if (j != 0 &&
                (inside(rings[j], rings[i].front()) || inside(rings[i], rings[j].front())))
            {
                return fail(error, "Holes must not overlap or nest");
            }
        }
    }
    return true;
}

bool triangulate_impl(const VertexLookup& lookup, const schema::ModelingFaceT& face,
                      std::vector<std::uint32_t>& output, std::string& error)
{
    output.clear();
    error.clear();
    std::vector<Ring> rings;
    std::vector<std::uint64_t> vertices;
    if (!rings_for_face(lookup, face, rings, vertices, error))
    {
        return false;
    }
    auto triangles = mapbox::earcut<std::uint32_t>(rings);
    if (triangles.empty() || triangles.size() % 3 != 0)
    {
        return fail(error, "Polygon triangulation failed");
    }
    double expected_area = std::abs(signed_area(rings.front()));
    for (std::size_t i = 1; i < rings.size(); ++i)
    {
        expected_area -= std::abs(signed_area(rings[i]));
    }
    const auto normal = lookup_normal(lookup, face);
    const auto axis = math::projection_axis(normal);
    double actual_area = 0.0;
    for (std::size_t i = 0; i < triangles.size(); i += 3)
    {
        if (triangles[i] >= vertices.size() || triangles[i + 1] >= vertices.size() ||
            triangles[i + 2] >= vertices.size())
        {
            return fail(error, "Triangulation produced an out-of-range corner index");
        }
        const auto a = lookup_position(lookup, vertices[triangles[i]]);
        const auto b = lookup_position(lookup, vertices[triangles[i + 1]]);
        const auto c = lookup_position(lookup, vertices[triangles[i + 2]]);
        const auto triangle_normal = math::cross(math::sub(b, a), math::sub(c, a));
        if (math::length(triangle_normal) <= math::epsilon)
        {
            return fail(error, "Triangulation produced a degenerate triangle");
        }
        if (math::dot(triangle_normal, normal) < 0.0)
        {
            std::swap(triangles[i + 1], triangles[i + 2]);
        }
        actual_area += std::abs(orientation(math::project(a, axis), math::project(b, axis),
                                            math::project(c, axis))) *
                       0.5;
    }
    if (std::abs(actual_area - expected_area) > std::max(1.0, expected_area) * 1.0e-7)
    {
        return fail(error, "Triangulation does not fully cover the polygon");
    }
    output = std::move(triangles);
    return true;
}

} // namespace

const schema::ModelingVertex* mesh::find_vertex(const schema::EditableMeshDataT& data,
                                                std::uint64_t id) noexcept
{
    const auto found = std::find_if(data.vertices.begin(), data.vertices.end(),
                                    [id](const auto& vertex) { return vertex.id() == id; });
    return found == data.vertices.end() ? nullptr : &*found;
}

schema::ModelingFaceT* mesh::find_face(schema::EditableMeshDataT& data, std::uint64_t id) noexcept
{
    const auto found = std::find_if(data.faces.begin(), data.faces.end(),
                                    [id](const auto& face) { return face && face->id == id; });
    return found == data.faces.end() ? nullptr : found->get();
}

const schema::ModelingFaceT* mesh::find_face(const schema::EditableMeshDataT& data,
                                             std::uint64_t id) noexcept
{
    const auto found = std::find_if(data.faces.begin(), data.faces.end(),
                                    [id](const auto& face) { return face && face->id == id; });
    return found == data.faces.end() ? nullptr : found->get();
}

bool mesh::validate(const schema::EditableMeshDataT& data, std::string& error)
{
    error.clear();
    if (data.schema_version != 2 || data.next_id == 0 ||
        data.next_id > std::numeric_limits<std::uint64_t>::max() - 10000000 ||
        data.vertices.size() > 1000000 || data.faces.size() > 1000000)
    {
        return fail(error, "Invalid model version, identity counter or element count");
    }
    if (data.primitive && !detail::validate_primitive(*data.primitive, error))
    {
        return false;
    }
    std::unordered_set<std::uint64_t> ids;
    for (const auto& vertex : data.vertices)
    {
        if (!vertex.id() || vertex.id() >= data.next_id || !ids.insert(vertex.id()).second ||
            !math::finite(math::position(vertex)) || math::length(math::position(vertex)) > 1.0e12)
        {
            return fail(error, "Invalid vertex identity or position");
        }
    }
    const auto lookup = make_lookup(data);
    for (const auto& face : data.faces)
    {
        if (!face || !face->id || face->id >= data.next_id || !ids.insert(face->id).second ||
            face->material_slot >= 16)
        {
            return fail(error, "Invalid face identity or material slot");
        }
        std::vector<Ring> rings;
        std::vector<std::uint64_t> vertices;
        if (!rings_for_face(lookup, *face, rings, vertices, error))
        {
            error = "Face " + std::to_string(face->id) + ": " + error;
            return false;
        }
    }
    return true;
}

bool mesh::triangulate_face(const schema::EditableMeshDataT& data,
                            const schema::ModelingFaceT& face, std::vector<std::uint32_t>& output,
                            std::string& error)
{
    return triangulate_impl(make_lookup(data), face, output, error);
}

bool mesh::compile(const schema::EditableMeshDataT& data, std::string_view path,
                   schema::MeshDataT& output, std::string& error)
{
    output = {};
    if (!validate(data, error) || data.faces.empty())
    {
        if (error.empty())
        {
            error = "An empty model cannot be compiled into a render mesh";
        }
        return false;
    }
    const auto lookup = make_lookup(data);
    schema::MeshDataT result;
    result.path = path;
    std::map<std::pair<std::uint64_t, std::uint32_t>, Vector> smooth_normals;
    for (const auto& face : data.faces)
    {
        if (!face->smoothing_group)
        {
            continue;
        }
        const auto normal = lookup_normal(lookup, *face);
        const auto accumulate = [&](const auto& ids)
        {
            for (auto id : ids)
            {
                auto& value = smooth_normals[{id, face->smoothing_group}];
                value = math::add(value, normal);
            }
        };
        accumulate(face->vertices);
        for (const auto& hole : face->holes)
        {
            accumulate(hole->vertices);
        }
    }
    std::map<std::uint32_t, std::vector<const schema::ModelingFaceT*>> materials;
    for (const auto& face : data.faces)
    {
        materials[face->material_slot].push_back(face.get());
    }
    for (const auto& [material, faces] : materials)
    {
        const auto first_index = static_cast<std::uint32_t>(result.indices.size());
        for (const auto* face : faces)
        {
            std::vector<std::uint32_t> indices;
            if (!triangulate_impl(lookup, *face, indices, error))
            {
                return false;
            }
            const auto offset = static_cast<std::uint32_t>(result.vertices.size());
            const auto flat_normal = math::normalized(lookup_normal(lookup, *face));
            const auto axis = math::projection_axis(flat_normal);
            const auto append_ring = [&](const auto& ids, const auto& uv, const auto& colors)
            {
                for (std::size_t i = 0; i < ids.size(); ++i)
                {
                    const auto point = lookup_position(lookup, ids[i]);
                    auto normal =
                        face->smoothing_group
                            ? math::normalized(smooth_normals[{ids[i], face->smoothing_group}])
                            : flat_normal;
                    if (math::length(normal) <= math::epsilon)
                    {
                        normal = flat_normal;
                    }
                    result.vertices.emplace_back(
                        static_cast<float>(point[0]), static_cast<float>(point[1]),
                        static_cast<float>(point[2]), static_cast<float>(normal[0]),
                        static_cast<float>(normal[1]), static_cast<float>(normal[2]));
                    const auto planar = math::project(point, axis);
                    result.texcoords.push_back(uv.empty() ? schema::Vec2(planar[0], planar[1])
                                                          : uv[i]);
                    result.colors.push_back(colors.empty() ? schema::Color(1, 1, 1, 1) : colors[i]);
                }
            };
            append_ring(face->vertices, face->texcoords, face->colors);
            for (const auto& hole : face->holes)
            {
                append_ring(hole->vertices, hole->texcoords, hole->colors);
            }
            for (auto index : indices)
            {
                result.indices.push_back(offset + index);
            }
        }
        result.submeshes.emplace_back(
            first_index, static_cast<std::uint32_t>(result.indices.size()) - first_index, material);
    }
    if (!MeshUtils::validate(result))
    {
        return fail(error, "Invalid compiled mesh");
    }
    if (!editor::generate_mesh_tangents(result, error))
    {
        error = "Failed to generate mesh tangents: " + error;
        return false;
    }
    output = std::move(result);
    return true;
}

bool mesh::from_mesh(const schema::MeshDataT& data, schema::EditableMeshDataT& output,
                     std::string& error)
{
    output = {};
    error.clear();
    if (!MeshUtils::validate(data))
    {
        return fail(error, "Invalid input render mesh");
    }
    schema::EditableMeshDataT result;
    result.schema_version = 2;
    std::map<std::tuple<float, float, float>, std::uint64_t> positions;
    std::vector<std::uint64_t> ids;
    for (const auto& vertex : data.vertices)
    {
        const auto key = std::make_tuple(vertex.x(), vertex.y(), vertex.z());
        auto found = positions.find(key);
        if (found == positions.end())
        {
            const auto id = math::add_vertex(result, {vertex.x(), vertex.y(), vertex.z()});
            found = positions.emplace(key, id).first;
        }
        ids.push_back(found->second);
    }
    for (std::size_t i = 0; i < data.indices.size(); i += 3)
    {
        std::uint32_t material = 0;
        for (const auto& submesh : data.submeshes)
        {
            if (i >= submesh.first_index() && i < submesh.first_index() + submesh.index_count())
            {
                material = submesh.material_slot();
                break;
            }
        }
        auto& face = math::add_face(
            result, {ids[data.indices[i]], ids[data.indices[i + 1]], ids[data.indices[i + 2]]},
            material);
        for (std::size_t j = i; j < i + 3; ++j)
        {
            const auto index = data.indices[j];
            if (!data.texcoords.empty())
            {
                face.texcoords.push_back(data.texcoords[index]);
            }
            if (!data.colors.empty())
            {
                face.colors.push_back(data.colors[index]);
            }
        }
    }
    // 以共享边两端的输入法线判断平滑连通，保留硬边与 UV 接缝的独立含义。
    struct SmoothEdge
    {
        std::size_t face = 0;
        Vector first{};
        Vector second{};
    };
    std::vector<std::size_t> parents(result.faces.size());
    for (std::size_t i = 0; i < parents.size(); ++i)
    {
        parents[i] = i;
    }
    const auto root = [&](std::size_t index)
    {
        while (parents[index] != index)
        {
            parents[index] = parents[parents[index]];
            index = parents[index];
        }
        return index;
    };
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<SmoothEdge>> shared;
    for (std::size_t triangle = 0; triangle < result.faces.size(); ++triangle)
    {
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const auto first_index = data.indices[triangle * 3 + corner];
            const auto second_index = data.indices[triangle * 3 + (corner + 1) % 3];
            const auto& first_vertex = data.vertices[first_index];
            const auto& second_vertex = data.vertices[second_index];
            const auto first_id = ids[first_index];
            const auto second_id = ids[second_index];
            const auto key = std::minmax(first_id, second_id);
            Vector first_normal =
                math::normalized({first_vertex.nx(), first_vertex.ny(), first_vertex.nz()});
            Vector second_normal =
                math::normalized({second_vertex.nx(), second_vertex.ny(), second_vertex.nz()});
            if (first_id != key.first)
            {
                std::swap(first_normal, second_normal);
            }
            auto& neighbors = shared[key];
            for (const auto& neighbor : neighbors)
            {
                if (math::dot(first_normal, neighbor.first) >= 1.0 - 1.0e-6 &&
                    math::dot(second_normal, neighbor.second) >= 1.0 - 1.0e-6)
                {
                    const auto a = root(triangle);
                    const auto b = root(neighbor.face);
                    parents[a] = b;
                }
            }
            neighbors.push_back({triangle, first_normal, second_normal});
        }
    }
    std::map<std::size_t, std::uint32_t> groups;
    for (std::size_t i = 0; i < result.faces.size(); ++i)
    {
        const auto component = root(i);
        auto found = groups.find(component);
        if (found == groups.end())
        {
            found = groups.emplace(component, static_cast<std::uint32_t>(groups.size() + 1)).first;
        }
        result.faces[i]->smoothing_group = found->second;
    }
    if (!validate(result, error))
    {
        return false;
    }
    output = std::move(result);
    return true;
}

bool mesh::serialize(const schema::EditableMeshDataT& data, std::vector<std::uint8_t>& output,
                     std::string& error)
{
    output.clear();
    if (!validate(data, error))
    {
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishEditableMeshDataBuffer(builder, schema::EditableMeshData::Pack(builder, &data));
    output.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
    return true;
}

bool mesh::deserialize(std::span<const std::uint8_t> bytes, schema::EditableMeshDataT& output,
                       std::string& error)
{
    output = {};
    error.clear();
    if (bytes.empty() || bytes.size() > 256 * 1024 * 1024)
    {
        return fail(error, "Model data is empty or exceeds 256 MiB");
    }
    flatbuffers::Verifier verifier(bytes.data(), bytes.size());
    if (!schema::VerifyEditableMeshDataBuffer(verifier))
    {
        return fail(error, "Model FlatBuffers verification failed");
    }
    auto decoded = schema::UnPackEditableMeshData(bytes.data());
    if (!decoded || !validate(*decoded, error))
    {
        return false;
    }
    output = std::move(*decoded);
    return true;
}

bool mesh::selected_vertices(const schema::EditableMeshDataT& data, const MeshSelection& selection,
                             std::vector<std::uint64_t>& output, std::string& error)
{
    output.clear();
    error.clear();
    std::set<std::uint64_t> selected;
    if (selection.all)
    {
        for (const auto& vertex : data.vertices)
        {
            selected.insert(vertex.id());
        }
    }
    for (auto id : selection.vertices)
    {
        if (!find_vertex(data, id))
        {
            return fail(error, "Selection contains a missing vertex");
        }
        selected.insert(id);
    }
    for (const auto& edge : selection.edges)
    {
        bool found = false;
        const auto contains = [&](const auto& ring)
        {
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                const auto a = ring[i];
                const auto b = ring[(i + 1) % ring.size()];
                if ((a == edge.first && b == edge.second) || (a == edge.second && b == edge.first))
                {
                    return true;
                }
            }
            return false;
        };
        for (const auto& face : data.faces)
        {
            found = found || contains(face->vertices);
            for (const auto& hole : face->holes)
            {
                found = found || contains(hole->vertices);
            }
        }
        if (!found)
        {
            return fail(error, "Selection contains a missing edge");
        }
        selected.insert(edge.first);
        selected.insert(edge.second);
    }
    for (auto id : selection.faces)
    {
        const auto* face = find_face(data, id);
        if (!face)
        {
            return fail(error, "Selection contains a missing face");
        }
        selected.insert(face->vertices.begin(), face->vertices.end());
        for (const auto& hole : face->holes)
        {
            selected.insert(hole->vertices.begin(), hole->vertices.end());
        }
    }
    if (selected.empty())
    {
        return fail(error, "Selection is empty");
    }
    output.assign(selected.begin(), selected.end());
    return true;
}

} // namespace engine::plugins::modeling
