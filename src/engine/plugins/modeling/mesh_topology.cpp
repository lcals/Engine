#include "plugins/modeling/mesh_topology.hpp"

#include "plugins/modeling/modeling_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace engine
{
namespace
{

using namespace modeling_detail;
using Id = std::uint64_t;
using EdgeKey = std::pair<Id, Id>;

bool fail(std::string& error, std::string message)
{
    error = std::move(message);
    return false;
}

EdgeKey edge_key(Id a, Id b)
{
    return std::minmax(a, b);
}

bool face_selection(const schema::EditableMeshDataT& data, std::span<const Id> faces,
                    std::set<Id>& result, std::string& error)
{
    if (!EditableMesh::validate(data, error))
    {
        return false;
    }
    if (faces.empty())
    {
        return fail(error, "面选区为空");
    }
    for (auto id : faces)
    {
        if (!EditableMesh::find_face(data, id) || !result.insert(id).second)
        {
            return fail(error, "面选区包含不存在或重复的身份");
        }
    }
    return true;
}

void remove_orphans(schema::EditableMeshDataT& data)
{
    std::unordered_set<Id> used;
    for (const auto& face : data.faces)
    {
        used.insert(face->vertices.begin(), face->vertices.end());
        for (const auto& hole : face->holes)
        {
            used.insert(hole->vertices.begin(), hole->vertices.end());
        }
    }
    std::erase_if(data.vertices, [&](const auto& vertex) { return !used.contains(vertex.id()); });
}

bool publish(schema::EditableMeshDataT& data, schema::EditableMeshDataT& result, std::string& error)
{
    if (!EditableMesh::validate(result, error))
    {
        return false;
    }
    data = std::move(result);
    error.clear();
    return true;
}

void map_face(schema::ModelingFaceT& face, const std::map<Id, Id>& mapping)
{
    const auto apply = [&](auto& ids)
    {
        for (auto& id : ids)
        {
            const auto found = mapping.find(id);
            if (found != mapping.end())
            {
                id = found->second;
            }
        }
    };
    apply(face.vertices);
    for (auto& hole : face.holes)
    {
        apply(hole->vertices);
    }
}

std::vector<Id> face_vertices(const schema::ModelingFaceT& face)
{
    auto ids = face.vertices;
    for (const auto& hole : face.holes)
    {
        ids.insert(ids.end(), hole->vertices.begin(), hole->vertices.end());
    }
    return ids;
}

bool boundary_loop(const schema::EditableMeshDataT& data, std::span<const Id> loop,
                   std::string& error)
{
    if (loop.size() < 3)
    {
        return fail(error, "边界环至少需要 3 个顶点");
    }
    std::set<Id> unique;
    for (auto id : loop)
    {
        if (!EditableMesh::find_vertex(data, id) || !unique.insert(id).second)
        {
            return fail(error, "边界环包含缺失或重复的顶点");
        }
    }
    std::map<EdgeKey, std::size_t> edges;
    const auto count = [&](const auto& ring)
    {
        for (std::size_t i = 0; i < ring.size(); ++i)
        {
            ++edges[edge_key(ring[i], ring[(i + 1) % ring.size()])];
        }
    };
    for (const auto& face : data.faces)
    {
        count(face->vertices);
        for (const auto& hole : face->holes)
        {
            count(hole->vertices);
        }
    }
    for (std::size_t i = 0; i < loop.size(); ++i)
    {
        if (edges[edge_key(loop[i], loop[(i + 1) % loop.size()])] != 1)
        {
            return fail(error, "操作需要完整的开放边界环");
        }
    }
    return true;
}

int boundary_direction(const schema::EditableMeshDataT& data, Id first, Id second)
{
    for (const auto& face : data.faces)
    {
        const auto inspect = [&](const auto& ring, bool hole)
        {
            bool reversed = false;
            if (hole)
            {
                schema::ModelingFaceT temporary;
                temporary.vertices = ring;
                reversed = dot(face_normal(data, temporary), face_normal(data, *face)) > 0.0;
            }
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                const auto a = ring[i];
                const auto b = ring[(i + 1) % ring.size()];
                if (a == first && b == second)
                {
                    return reversed ? -1 : 1;
                }
                if (a == second && b == first)
                {
                    return reversed ? 1 : -1;
                }
            }
            return 0;
        };
        const auto outer = inspect(face->vertices, false);
        if (outer)
        {
            return outer;
        }
        for (const auto& hole : face->holes)
        {
            const auto inner = inspect(hole->vertices, true);
            if (inner)
            {
                return inner;
            }
        }
    }
    return 0;
}

schema::Vec2 mix(const schema::Vec2& a, const schema::Vec2& b, double t)
{
    return {a.x() * (1.0 - t) + b.x() * t, a.y() * (1.0 - t) + b.y() * t};
}

schema::Color mix(const schema::Color& a, const schema::Color& b, double t)
{
    const auto s = static_cast<float>(t);
    return {a.r() * (1.0F - s) + b.r() * s, a.g() * (1.0F - s) + b.g() * s,
            a.b() * (1.0F - s) + b.b() * s, a.a() * (1.0F - s) + b.a() * s};
}

void clean_ring(std::vector<Id>& ids, std::vector<schema::Vec2>& uv,
                std::vector<schema::Color>& colors)
{
    for (std::size_t i = 0; ids.size() > 1 && i < ids.size();)
    {
        const auto next = (i + 1) % ids.size();
        if (ids[i] != ids[next])
        {
            ++i;
            continue;
        }
        ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(i));
        if (!uv.empty())
        {
            uv.erase(uv.begin() + static_cast<std::ptrdiff_t>(i));
        }
        if (!colors.empty())
        {
            colors.erase(colors.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
}

void remap_and_clean(schema::EditableMeshDataT& data, const std::map<Id, Id>& mapping)
{
    for (auto& face : data.faces)
    {
        map_face(*face, mapping);
        clean_ring(face->vertices, face->texcoords, face->colors);
        for (auto& hole : face->holes)
        {
            clean_ring(hole->vertices, hole->texcoords, hole->colors);
        }
        std::erase_if(face->holes, [](const auto& hole) { return hole->vertices.size() < 3; });
    }
    std::erase_if(data.faces, [](const auto& face) { return face->vertices.size() < 3; });
    remove_orphans(data);
}

} // namespace

bool MeshTopology::transform(schema::EditableMeshDataT& data, const MeshSelection& selection,
                             const std::array<double, 16>& matrix, std::string& error)
{
    if (!EditableMesh::validate(data, error))
    {
        return false;
    }
    for (auto value : matrix)
    {
        if (!std::isfinite(value))
        {
            return fail(error, "变换矩阵必须为有限数值");
        }
    }
    if (std::abs(matrix[3]) > epsilon || std::abs(matrix[7]) > epsilon ||
        std::abs(matrix[11]) > epsilon || std::abs(matrix[15] - 1.0) > epsilon)
    {
        return fail(error, "建模变换只接受仿射矩阵");
    }
    std::vector<Id> selected;
    if (!EditableMesh::selected_vertices(data, selection, selected, error))
    {
        return false;
    }
    schema::EditableMeshDataT result(data);
    const std::unordered_set<Id> ids(selected.begin(), selected.end());
    for (auto& vertex : result.vertices)
    {
        if (!ids.contains(vertex.id()))
        {
            continue;
        }
        const auto p = position(vertex);
        vertex = schema::ModelingVertex(
            vertex.id(), matrix[0] * p[0] + matrix[4] * p[1] + matrix[8] * p[2] + matrix[12],
            matrix[1] * p[0] + matrix[5] * p[1] + matrix[9] * p[2] + matrix[13],
            matrix[2] * p[0] + matrix[6] * p[1] + matrix[10] * p[2] + matrix[14]);
    }
    // 点或边编辑可以使原多边形弯曲；沿变换前的合法三角化拆分受影响面。
    std::vector<Id> non_planar;
    for (const auto& face : result.faces)
    {
        const auto vertices = face_vertices(*face);
        const auto selected_count = static_cast<std::size_t>(std::count_if(
            vertices.begin(), vertices.end(), [&](auto id) { return ids.contains(id); }));
        if (selected_count == 0 || selected_count == vertices.size() || vertices.size() == 3)
        {
            continue;
        }
        const auto normal = normalized(face_normal(result, *face));
        const auto origin = position(result, vertices.front());
        double scale = 1.0;
        for (auto id : vertices)
        {
            scale = std::max(scale, length(sub(position(result, id), origin)));
        }
        if (length(normal) <= epsilon ||
            std::any_of(vertices.begin(), vertices.end(),
                        [&](auto id)
                        {
                            return std::abs(dot(sub(position(result, id), origin), normal)) >
                                   1.0e-7 * scale;
                        }))
        {
            non_planar.push_back(face->id);
        }
    }
    if (!non_planar.empty())
    {
        schema::EditableMeshDataT triangulated(data);
        if (!MeshTopology::triangulate(triangulated, non_planar, error))
        {
            return false;
        }
        result.faces = std::move(triangulated.faces);
        result.next_id = triangulated.next_id;
    }
    const Vector column0{matrix[0], matrix[1], matrix[2]};
    const Vector column1{matrix[4], matrix[5], matrix[6]};
    const Vector column2{matrix[8], matrix[9], matrix[10]};
    if (dot(column0, cross(column1, column2)) < 0.0)
    {
        for (auto& face : result.faces)
        {
            const auto face_ids = face_vertices(*face);
            if (std::all_of(face_ids.begin(), face_ids.end(),
                            [&](auto id) { return ids.contains(id); }))
            {
                reverse_face(*face);
            }
        }
    }
    return publish(data, result, error);
}

bool MeshTopology::extrude(schema::EditableMeshDataT& data, std::span<const Id> faces,
                           double distance, bool individual, std::string& error)
{
    std::set<Id> selected;
    if (!face_selection(data, faces, selected, error))
    {
        return false;
    }
    if (!std::isfinite(distance) || std::abs(distance) <= epsilon)
    {
        return fail(error, "挤出距离必须为有限非零值");
    }
    schema::EditableMeshDataT result(data);
    std::vector<std::vector<Id>> groups;
    if (individual)
    {
        for (auto id : selected)
        {
            groups.push_back({id});
        }
    }
    else
    {
        groups.emplace_back(selected.begin(), selected.end());
    }
    for (const auto& group : groups)
    {
        struct Boundary
        {
            Id first = 0;
            Id second = 0;
            std::uint32_t material = 0;
            std::size_t count = 0;
        };
        std::map<EdgeKey, Boundary> boundaries;
        std::map<Id, Vector> normals;
        const auto add_ring = [&](const auto& ring, const Vector& normal,
                                  std::uint32_t material) -> bool
        {
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                const auto a = ring[i];
                const auto b = ring[(i + 1) % ring.size()];
                auto& edge = boundaries[edge_key(a, b)];
                if (edge.count && (edge.count > 1 || edge.first == a))
                {
                    return fail(error, "挤出选区包含非流形边或不一致绕序");
                }
                if (!edge.count)
                {
                    edge.first = a;
                    edge.second = b;
                    edge.material = material;
                }
                ++edge.count;
                normals[a] = add(normals[a], normal);
            }
            return true;
        };
        for (auto id : group)
        {
            const auto* face = EditableMesh::find_face(data, id);
            const auto normal = face_normal(data, *face);
            if (!add_ring(face->vertices, normal, face->material_slot))
            {
                return false;
            }
            for (const auto& hole : face->holes)
            {
                auto ring = hole->vertices;
                schema::ModelingFaceT temporary;
                temporary.vertices = ring;
                if (dot(face_normal(data, temporary), normal) > 0.0)
                {
                    std::reverse(ring.begin(), ring.end());
                }
                if (!add_ring(ring, normal, face->material_slot))
                {
                    return false;
                }
            }
        }
        std::map<Id, Id> replacement;
        for (const auto& [id, normal] : normals)
        {
            const auto direction = normalized(normal);
            if (length(direction) <= epsilon)
            {
                return fail(error, "挤出方向无法确定");
            }
            replacement[id] = add_vertex(result, add(position(data, id), mul(direction, distance)));
        }
        for (auto id : group)
        {
            map_face(*EditableMesh::find_face(result, id), replacement);
        }
        for (const auto& [key, edge] : boundaries)
        {
            (void)key;
            if (edge.count == 1)
            {
                auto& side = add_face(
                    result,
                    {edge.first, edge.second, replacement[edge.second], replacement[edge.first]},
                    edge.material);
                side.texcoords = {
                    {0, 0},
                    {length(sub(position(data, edge.second), position(data, edge.first))), 0},
                    {length(sub(position(data, edge.second), position(data, edge.first))),
                     std::abs(distance)},
                    {0, std::abs(distance)}};
            }
        }
    }
    remove_orphans(result);
    return publish(data, result, error);
}

bool MeshTopology::extrude_edges(schema::EditableMeshDataT& data, std::span<const MeshEdge> edges,
                                 const schema::Vec3& delta, std::string& error)
{
    if (!EditableMesh::validate(data, error))
    {
        return false;
    }
    const Vector movement{delta.x(), delta.y(), delta.z()};
    if (edges.empty() || !finite(movement) || length(movement) <= epsilon)
    {
        return fail(error, "边挤出需要非空边选区和有限非零位移");
    }
    struct EdgeSource
    {
        std::uint32_t material = 0;
        std::size_t count = 0;
    };
    std::map<EdgeKey, EdgeSource> incident;
    for (const auto& face : data.faces)
    {
        const auto append_ring = [&](const auto& ring)
        {
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                auto& source = incident[edge_key(ring[i], ring[(i + 1) % ring.size()])];
                source.material = face->material_slot;
                ++source.count;
            }
        };
        append_ring(face->vertices);
        for (const auto& hole : face->holes)
        {
            append_ring(hole->vertices);
        }
    }
    std::set<EdgeKey> selected;
    std::map<Id, Id> replacements;
    schema::EditableMeshDataT result(data);
    for (const auto& edge : edges)
    {
        const auto key = edge_key(edge.first, edge.second);
        const auto found = incident.find(key);
        if (found == incident.end() || found->second.count != 1 || !selected.insert(key).second)
        {
            return fail(error, "边挤出只接受不重复的开放边界边");
        }
        for (auto id : {edge.first, edge.second})
        {
            if (!replacements.contains(id))
            {
                replacements[id] = add_vertex(result, add(position(data, id), movement));
            }
        }
        auto& face = add_face(
            result, {edge.first, edge.second, replacements[edge.second], replacements[edge.first]},
            found->second.material);
        const auto width = length(sub(position(data, edge.second), position(data, edge.first)));
        face.texcoords = {{0, 0}, {width, 0}, {width, length(movement)}, {0, length(movement)}};
        if (boundary_direction(data, edge.first, edge.second) > 0)
        {
            reverse_face(face);
        }
    }
    return publish(data, result, error);
}

namespace
{

bool inset_region(schema::EditableMeshDataT& data, const std::set<Id>& selected, double distance,
                  std::string& error)
{
    struct FaceSamples
    {
        const schema::ModelingFaceT* face = nullptr;
        std::vector<Id> ids;
        std::vector<schema::Vec2> uv;
        std::vector<schema::Color> colors;
        std::vector<std::uint32_t> triangles;
    };
    struct Boundary
    {
        Id first = 0;
        Id second = 0;
        Id face = 0;
        std::size_t count = 0;
    };
    const auto* reference = EditableMesh::find_face(data, *selected.begin());
    const auto normal = normalized(face_normal(data, *reference));
    const auto origin = position(data, reference->vertices.front());
    const auto axis = projection_axis(normal);
    std::map<Id, FaceSamples> samples;
    std::map<EdgeKey, Boundary> edges;
    double scale = 1.0;
    for (auto id : selected)
    {
        const auto* face = EditableMesh::find_face(data, id);
        if (dot(normalized(face_normal(data, *face)), normal) < 1.0 - 1.0e-7)
        {
            return fail(error, "整体内插选区必须同向共面");
        }
        for (auto vertex : face_vertices(*face))
        {
            scale = std::max(scale, length(sub(position(data, vertex), origin)));
        }
    }
    for (auto id : selected)
    {
        const auto* face = EditableMesh::find_face(data, id);
        auto& sample = samples[id];
        sample.face = face;
        const auto append_ring =
            [&](const auto& ring, const auto& uv, const auto& colors, bool hole)
        {
            auto directed = ring;
            if (hole)
            {
                schema::ModelingFaceT temporary;
                temporary.vertices = ring;
                if (dot(face_normal(data, temporary), normal) > 0.0)
                {
                    std::reverse(directed.begin(), directed.end());
                }
            }
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                const auto point = position(data, ring[i]);
                if (std::abs(dot(sub(point, origin), normal)) > 1.0e-7 * scale)
                {
                    return fail(error, "整体内插选区不共面");
                }
                const auto projected = project(point, axis);
                sample.ids.push_back(ring[i]);
                sample.uv.push_back(uv.empty() ? schema::Vec2(projected[0], projected[1]) : uv[i]);
                sample.colors.push_back(colors.empty() ? schema::Color(1, 1, 1, 1) : colors[i]);
                const auto a = directed[i];
                const auto b = directed[(i + 1) % directed.size()];
                auto& boundary = edges[edge_key(a, b)];
                if (boundary.count && (boundary.count > 1 || boundary.first == a))
                {
                    return fail(error, "整体内插选区包含非流形或绕序冲突边");
                }
                if (!boundary.count)
                {
                    boundary.first = a;
                    boundary.second = b;
                    boundary.face = id;
                }
                ++boundary.count;
            }
            return true;
        };
        if (!append_ring(face->vertices, face->texcoords, face->colors, false))
        {
            return false;
        }
        for (const auto& hole : face->holes)
        {
            if (!append_ring(hole->vertices, hole->texcoords, hole->colors, true))
            {
                return false;
            }
        }
        if (!EditableMesh::triangulate_face(data, *face, sample.triangles, error))
        {
            return false;
        }
    }
    std::map<Id, Id> previous;
    std::map<Id, Id> next;
    for (const auto& [key, boundary] : edges)
    {
        (void)key;
        if (boundary.count != 1)
        {
            continue;
        }
        if (!next.emplace(boundary.first, boundary.second).second ||
            !previous.emplace(boundary.second, boundary.first).second)
        {
            return fail(error, "整体内插选区的边界在顶点处分叉");
        }
    }
    if (next.empty() || next.size() != previous.size())
    {
        return fail(error, "整体内插需要闭合的开放选区边界");
    }
    const auto interpolate = [&](const FaceSamples& sample, const Vector& point,
                                 bool require_inside, schema::Vec2& uv, schema::Color& color)
    {
        double best_score = std::numeric_limits<double>::max();
        std::array<std::uint32_t, 3> best_triangle{};
        std::array<double, 3> best_weights{};
        for (std::size_t i = 0; i < sample.triangles.size(); i += 3)
        {
            const auto a = position(data, sample.ids[sample.triangles[i]]);
            const auto b = position(data, sample.ids[sample.triangles[i + 1]]);
            const auto c = position(data, sample.ids[sample.triangles[i + 2]]);
            const auto ab = sub(b, a);
            const auto ac = sub(c, a);
            const auto ap = sub(point, a);
            const auto aa = dot(ab, ab);
            const auto cc = dot(ac, ac);
            const auto mixed = dot(ab, ac);
            const auto determinant = aa * cc - mixed * mixed;
            if (std::abs(determinant) <= epsilon * epsilon)
            {
                continue;
            }
            const auto v = (cc * dot(ap, ab) - mixed * dot(ap, ac)) / determinant;
            const auto w = (aa * dot(ap, ac) - mixed * dot(ap, ab)) / determinant;
            const std::array<double, 3> weights{1.0 - v - w, v, w};
            double score = 0.0;
            for (auto weight : weights)
            {
                score += std::max(0.0, -weight);
            }
            if (score < best_score)
            {
                best_score = score;
                best_triangle = {sample.triangles[i], sample.triangles[i + 1],
                                 sample.triangles[i + 2]};
                best_weights = weights;
            }
            if (score <= epsilon)
            {
                break;
            }
        }
        if (best_score == std::numeric_limits<double>::max() ||
            (require_inside && best_score > epsilon))
        {
            return false;
        }
        double u = 0;
        double v = 0;
        std::array<double, 4> channels{};
        for (std::size_t i = 0; i < 3; ++i)
        {
            u += sample.uv[best_triangle[i]].x() * best_weights[i];
            v += sample.uv[best_triangle[i]].y() * best_weights[i];
            const auto& source = sample.colors[best_triangle[i]];
            channels[0] += source.r() * best_weights[i];
            channels[1] += source.g() * best_weights[i];
            channels[2] += source.b() * best_weights[i];
            channels[3] += source.a() * best_weights[i];
        }
        uv = {u, v};
        color = {static_cast<float>(std::clamp(channels[0], 0.0, 1.0)),
                 static_cast<float>(std::clamp(channels[1], 0.0, 1.0)),
                 static_cast<float>(std::clamp(channels[2], 0.0, 1.0)),
                 static_cast<float>(std::clamp(channels[3], 0.0, 1.0))};
        return true;
    };
    schema::EditableMeshDataT result(data);
    std::map<Id, Id> replacements;
    for (const auto& [id, following] : next)
    {
        const auto incoming = previous.find(id);
        if (incoming == previous.end())
        {
            return fail(error, "整体内插边界不闭合");
        }
        const auto point = position(data, id);
        const auto inward0 =
            normalized(cross(normal, sub(point, position(data, incoming->second))));
        const auto inward1 = normalized(cross(normal, sub(position(data, following), point)));
        const auto bisector = add(inward0, inward1);
        const auto denominator = dot(bisector, inward0);
        if (std::abs(denominator) <= epsilon)
        {
            return fail(error, "整体内插边界包含退化折返");
        }
        const auto moved = add(point, mul(bisector, distance / denominator));
        bool inside = false;
        for (const auto& [face_id, sample] : samples)
        {
            (void)face_id;
            schema::Vec2 uv;
            schema::Color color;
            if (interpolate(sample, moved, true, uv, color))
            {
                inside = true;
                break;
            }
        }
        if (!inside)
        {
            return fail(error, "整体内插距离超过选区可用范围");
        }
        replacements[id] = add_vertex(result, moved);
    }
    for (auto id : selected)
    {
        auto* face = EditableMesh::find_face(result, id);
        const auto& sample = samples[id];
        const auto update_ring = [&](auto& ring, auto& uv, auto& colors)
        {
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                const auto found = replacements.find(ring[i]);
                if (found == replacements.end())
                {
                    continue;
                }
                schema::Vec2 value_uv;
                schema::Color value_color;
                if (!interpolate(sample, position(result, found->second), false, value_uv,
                                 value_color))
                {
                    return fail(error, "整体内插无法恢复面角属性");
                }
                ring[i] = found->second;
                if (!uv.empty())
                {
                    uv[i] = value_uv;
                }
                if (!colors.empty())
                {
                    colors[i] = value_color;
                }
            }
            return true;
        };
        if (!update_ring(face->vertices, face->texcoords, face->colors))
        {
            return false;
        }
        for (auto& hole : face->holes)
        {
            if (!update_ring(hole->vertices, hole->texcoords, hole->colors))
            {
                return false;
            }
        }
        if (dot(face_normal(result, *face), normal) <= epsilon)
        {
            return fail(error, "整体内插导致内部面反转");
        }
    }
    for (const auto& [key, boundary] : edges)
    {
        (void)key;
        if (boundary.count != 1)
        {
            continue;
        }
        const auto& sample = samples[boundary.face];
        auto& border = add_face(result,
                                {boundary.first, boundary.second, replacements[boundary.second],
                                 replacements[boundary.first]},
                                sample.face->material_slot, sample.face->smoothing_group);
        for (auto id : border.vertices)
        {
            schema::Vec2 uv;
            schema::Color color;
            if (!interpolate(sample, position(result, id), false, uv, color))
            {
                return fail(error, "整体内插无法恢复边框属性");
            }
            border.texcoords.push_back(uv);
            border.colors.push_back(color);
        }
        if (dot(face_normal(result, border), normal) <= epsilon)
        {
            return fail(error, "整体内插距离导致边框反转");
        }
    }
    return publish(data, result, error);
}

} // namespace

bool MeshTopology::inset(schema::EditableMeshDataT& data, std::span<const Id> faces,
                         double distance, std::string& error, bool group)
{
    std::set<Id> selected;
    if (!face_selection(data, faces, selected, error))
    {
        return false;
    }
    if (!std::isfinite(distance) || distance <= epsilon)
    {
        return fail(error, "内插距离必须为有限正值");
    }
    if (group)
    {
        return inset_region(data, selected, distance, error);
    }
    schema::EditableMeshDataT result(data);
    for (auto id : selected)
    {
        const auto* original = EditableMesh::find_face(data, id);
        if (!original->holes.empty())
        {
            return fail(error, "带洞面的内插需要先拆分为无洞面");
        }
        const auto normal = normalized(face_normal(data, *original));
        std::vector<std::uint32_t> triangles;
        if (!EditableMesh::triangulate_face(data, *original, triangles, error))
        {
            return false;
        }
        std::vector<Id> inner;
        std::vector<schema::Vec2> inner_uv;
        std::vector<schema::Color> inner_colors;
        for (std::size_t i = 0; i < original->vertices.size(); ++i)
        {
            const auto p = position(data, original->vertices[i]);
            const auto previous =
                position(data, original->vertices[(i + original->vertices.size() - 1) %
                                                  original->vertices.size()]);
            const auto next =
                position(data, original->vertices[(i + 1) % original->vertices.size()]);
            const auto inward0 = normalized(cross(normal, sub(p, previous)));
            const auto inward1 = normalized(cross(normal, sub(next, p)));
            const auto bisector = add(inward0, inward1);
            const auto denominator = dot(bisector, inward0);
            if (std::abs(denominator) <= epsilon)
            {
                return fail(error, "内插遇到折返或退化边");
            }
            const auto point = add(p, mul(bisector, distance / denominator));
            bool contained = false;
            for (std::size_t triangle = 0; triangle < triangles.size(); triangle += 3)
            {
                const auto a = position(data, original->vertices[triangles[triangle]]);
                const auto b = position(data, original->vertices[triangles[triangle + 1]]);
                const auto c = position(data, original->vertices[triangles[triangle + 2]]);
                const auto ab = sub(b, a);
                const auto ac = sub(c, a);
                const auto ap = sub(point, a);
                const auto aa = dot(ab, ab);
                const auto cc = dot(ac, ac);
                const auto aa_cc = dot(ab, ac);
                const auto determinant = aa * cc - aa_cc * aa_cc;
                if (std::abs(determinant) <= epsilon * epsilon)
                {
                    continue;
                }
                const auto weight_b = (cc * dot(ap, ab) - aa_cc * dot(ap, ac)) / determinant;
                const auto weight_c = (aa * dot(ap, ac) - aa_cc * dot(ap, ab)) / determinant;
                const auto weight_a = 1.0 - weight_b - weight_c;
                if (weight_a < -epsilon || weight_b < -epsilon || weight_c < -epsilon)
                {
                    continue;
                }
                const std::array<double, 3> weights{weight_a, weight_b, weight_c};
                if (!original->texcoords.empty())
                {
                    double u = 0.0;
                    double v = 0.0;
                    for (std::size_t corner = 0; corner < 3; ++corner)
                    {
                        const auto& uv = original->texcoords[triangles[triangle + corner]];
                        u += uv.x() * weights[corner];
                        v += uv.y() * weights[corner];
                    }
                    inner_uv.emplace_back(u, v);
                }
                if (!original->colors.empty())
                {
                    std::array<double, 4> color{};
                    for (std::size_t corner = 0; corner < 3; ++corner)
                    {
                        const auto& source = original->colors[triangles[triangle + corner]];
                        color[0] += source.r() * weights[corner];
                        color[1] += source.g() * weights[corner];
                        color[2] += source.b() * weights[corner];
                        color[3] += source.a() * weights[corner];
                    }
                    inner_colors.emplace_back(static_cast<float>(std::clamp(color[0], 0.0, 1.0)),
                                              static_cast<float>(std::clamp(color[1], 0.0, 1.0)),
                                              static_cast<float>(std::clamp(color[2], 0.0, 1.0)),
                                              static_cast<float>(std::clamp(color[3], 0.0, 1.0)));
                }
                contained = true;
                break;
            }
            if (!contained)
            {
                return fail(error, "内插结果离开了原始多边形内部");
            }
            inner.push_back(add_vertex(result, point));
        }
        auto* inner_face = EditableMesh::find_face(result, id);
        inner_face->vertices = inner;
        inner_face->texcoords = inner_uv;
        inner_face->colors = inner_colors;
        if (dot(face_normal(result, *inner_face), normal) <= epsilon)
        {
            return fail(error, "内插距离超过面的可用宽度");
        }
        for (std::size_t i = 0; i < inner.size(); ++i)
        {
            const auto next = (i + 1) % inner.size();
            if (dot(sub(position(result, inner[next]), position(result, inner[i])),
                    sub(position(data, original->vertices[next]),
                        position(data, original->vertices[i]))) <= epsilon)
            {
                return fail(error, "内插距离导致边反转");
            }
            auto& ring = add_face(
                result, {original->vertices[i], original->vertices[next], inner[next], inner[i]},
                original->material_slot, original->smoothing_group);
            if (!original->texcoords.empty())
            {
                ring.texcoords = {original->texcoords[i], original->texcoords[next], inner_uv[next],
                                  inner_uv[i]};
            }
            if (!original->colors.empty())
            {
                ring.colors = {original->colors[i], original->colors[next], inner_colors[next],
                               inner_colors[i]};
            }
        }
    }
    return publish(data, result, error);
}

bool MeshTopology::erase_faces(schema::EditableMeshDataT& data, std::span<const Id> faces,
                               std::string& error)
{
    std::set<Id> selected;
    if (!face_selection(data, faces, selected, error))
    {
        return false;
    }
    schema::EditableMeshDataT result(data);
    std::erase_if(result.faces, [&](const auto& face) { return selected.contains(face->id); });
    remove_orphans(result);
    return publish(data, result, error);
}

bool MeshTopology::reverse_faces(schema::EditableMeshDataT& data, std::span<const Id> faces,
                                 std::string& error)
{
    std::set<Id> selected;
    if (!face_selection(data, faces, selected, error))
    {
        return false;
    }
    schema::EditableMeshDataT result(data);
    for (auto id : selected)
    {
        reverse_face(*EditableMesh::find_face(result, id));
    }
    return publish(data, result, error);
}

bool MeshTopology::triangulate(schema::EditableMeshDataT& data, std::span<const Id> faces,
                               std::string& error)
{
    std::set<Id> selected;
    if (!face_selection(data, faces, selected, error))
    {
        return false;
    }
    schema::EditableMeshDataT result(data);
    std::erase_if(result.faces, [&](const auto& face) { return selected.contains(face->id); });
    for (auto id : selected)
    {
        const auto* source = EditableMesh::find_face(data, id);
        std::vector<std::uint32_t> triangles;
        if (!EditableMesh::triangulate_face(data, *source, triangles, error))
        {
            return false;
        }
        const auto ids = face_vertices(*source);
        std::vector<schema::Vec2> uv;
        std::vector<schema::Color> colors;
        const auto flatten = [&](const auto& ring, const auto& ring_uv, const auto& ring_colors)
        {
            const auto axis = projection_axis(face_normal(data, *source));
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                const auto projected = project(position(data, ring[i]), axis);
                uv.push_back(ring_uv.empty() ? schema::Vec2(projected[0], projected[1])
                                             : ring_uv[i]);
                colors.push_back(ring_colors.empty() ? schema::Color(1, 1, 1, 1) : ring_colors[i]);
            }
        };
        flatten(source->vertices, source->texcoords, source->colors);
        for (const auto& hole : source->holes)
        {
            flatten(hole->vertices, hole->texcoords, hole->colors);
        }
        for (std::size_t i = 0; i < triangles.size(); i += 3)
        {
            auto face = std::make_unique<schema::ModelingFaceT>();
            face->id = i == 0 ? source->id : result.next_id++;
            face->material_slot = source->material_slot;
            face->smoothing_group = source->smoothing_group;
            for (std::size_t j = i; j < i + 3; ++j)
            {
                face->vertices.push_back(ids[triangles[j]]);
                face->texcoords.push_back(uv[triangles[j]]);
                face->colors.push_back(colors[triangles[j]]);
            }
            result.faces.push_back(std::move(face));
        }
    }
    return publish(data, result, error);
}

bool MeshTopology::subdivide(schema::EditableMeshDataT& data, std::span<const Id> faces,
                             std::uint32_t iterations, std::string& error)
{
    std::set<Id> selected;
    if (!face_selection(data, faces, selected, error))
    {
        return false;
    }
    if (iterations < 1 || iterations > 5)
    {
        return fail(error, "细分次数必须为 1 到 5");
    }
    schema::EditableMeshDataT result(data);
    for (std::uint32_t iteration = 0; iteration < iterations; ++iteration)
    {
        schema::EditableMeshDataT before(result);
        std::map<EdgeKey, Id> midpoints;
        std::set<Id> next_selected;
        std::erase_if(result.faces, [&](const auto& face) { return selected.contains(face->id); });
        for (auto id : selected)
        {
            const auto* face = EditableMesh::find_face(before, id);
            if (!face->holes.empty())
            {
                return fail(error, "带洞面请先三角化再细分");
            }
            Vector center{};
            schema::Vec2 uv_center;
            std::array<double, 4> color_center{};
            double u = 0.0;
            double v = 0.0;
            for (std::size_t i = 0; i < face->vertices.size(); ++i)
            {
                center = add(center, position(before, face->vertices[i]));
                const auto uv = corner_uv(*face, i);
                const auto color = corner_color(*face, i);
                u += uv.x();
                v += uv.y();
                color_center[0] += color.r();
                color_center[1] += color.g();
                color_center[2] += color.b();
                color_center[3] += color.a();
            }
            const auto count = static_cast<double>(face->vertices.size());
            const auto center_id = add_vertex(result, mul(center, 1.0 / count));
            uv_center = {u / count, v / count};
            schema::Color center_color(static_cast<float>(color_center[0] / count),
                                       static_cast<float>(color_center[1] / count),
                                       static_cast<float>(color_center[2] / count),
                                       static_cast<float>(color_center[3] / count));
            std::vector<Id> middle;
            for (std::size_t i = 0; i < face->vertices.size(); ++i)
            {
                const auto a = face->vertices[i];
                const auto b = face->vertices[(i + 1) % face->vertices.size()];
                const auto key = edge_key(a, b);
                auto found = midpoints.find(key);
                if (found == midpoints.end())
                {
                    const auto mid =
                        add_vertex(result, mul(add(position(before, a), position(before, b)), 0.5));
                    found = midpoints.emplace(key, mid).first;
                }
                middle.push_back(found->second);
            }
            for (std::size_t i = 0; i < face->vertices.size(); ++i)
            {
                const auto next = (i + 1) % face->vertices.size();
                const auto previous = (i + face->vertices.size() - 1) % face->vertices.size();
                auto child = std::make_unique<schema::ModelingFaceT>();
                child->id = i == 0 ? face->id : result.next_id++;
                child->vertices = {face->vertices[i], middle[i], center_id, middle[previous]};
                child->material_slot = face->material_slot;
                child->smoothing_group = face->smoothing_group;
                if (!face->texcoords.empty())
                {
                    child->texcoords = {
                        corner_uv(*face, i), mix(corner_uv(*face, i), corner_uv(*face, next), 0.5),
                        uv_center, mix(corner_uv(*face, previous), corner_uv(*face, i), 0.5)};
                }
                if (!face->colors.empty())
                {
                    child->colors = {
                        corner_color(*face, i),
                        mix(corner_color(*face, i), corner_color(*face, next), 0.5), center_color,
                        mix(corner_color(*face, previous), corner_color(*face, i), 0.5)};
                }
                next_selected.insert(child->id);
                result.faces.push_back(std::move(child));
            }
        }
        // 未选相邻面同步插入共享边中点，避免 T 接缝。
        for (auto& face : result.faces)
        {
            if (next_selected.contains(face->id))
            {
                continue;
            }
            const auto split_ring = [&](auto& ids, auto& uv, auto& colors)
            {
                const auto old_ids = ids;
                const auto old_uv = uv;
                const auto old_colors = colors;
                ids.clear();
                uv.clear();
                colors.clear();
                for (std::size_t i = 0; i < old_ids.size(); ++i)
                {
                    const auto next = (i + 1) % old_ids.size();
                    ids.push_back(old_ids[i]);
                    if (!old_uv.empty())
                    {
                        uv.push_back(old_uv[i]);
                    }
                    if (!old_colors.empty())
                    {
                        colors.push_back(old_colors[i]);
                    }
                    const auto found = midpoints.find(edge_key(old_ids[i], old_ids[next]));
                    if (found != midpoints.end())
                    {
                        ids.push_back(found->second);
                        if (!old_uv.empty())
                        {
                            uv.push_back(mix(old_uv[i], old_uv[next], 0.5));
                        }
                        if (!old_colors.empty())
                        {
                            colors.push_back(mix(old_colors[i], old_colors[next], 0.5));
                        }
                    }
                }
            };
            split_ring(face->vertices, face->texcoords, face->colors);
            for (auto& hole : face->holes)
            {
                split_ring(hole->vertices, hole->texcoords, hole->colors);
            }
        }
        selected = std::move(next_selected);
        if (result.faces.size() > 1000000 || result.vertices.size() > 1000000)
        {
            return fail(error, "细分结果超过模型数量限制");
        }
    }
    return publish(data, result, error);
}

bool MeshTopology::weld(schema::EditableMeshDataT& data, std::span<const Id> vertices,
                        double tolerance, std::string& error)
{
    if (!EditableMesh::validate(data, error))
    {
        return false;
    }
    if (!std::isfinite(tolerance) || tolerance < 0.0 || vertices.size() < 2)
    {
        return fail(error, "焊接需要至少两个顶点和非负距离");
    }
    std::set<Id> selected(vertices.begin(), vertices.end());
    if (selected.size() != vertices.size())
    {
        return fail(error, "顶点选区包含重复身份");
    }
    for (auto id : selected)
    {
        if (!EditableMesh::find_vertex(data, id))
        {
            return fail(error, "顶点选区包含缺失身份");
        }
    }
    std::map<Id, Id> mapping;
    for (auto id : selected)
    {
        Id destination = id;
        for (auto other : selected)
        {
            if (other >= id)
            {
                break;
            }
            if (length(sub(position(data, id), position(data, other))) <= tolerance)
            {
                destination = mapping.contains(other) ? mapping[other] : other;
                break;
            }
        }
        mapping[id] = destination;
    }
    schema::EditableMeshDataT result(data);
    remap_and_clean(result, mapping);
    return publish(data, result, error);
}

bool MeshTopology::merge_vertices(schema::EditableMeshDataT& data, std::span<const Id> vertices,
                                  const schema::Vec3& point, std::string& error)
{
    if (!EditableMesh::validate(data, error))
    {
        return false;
    }
    const Vector value{point.x(), point.y(), point.z()};
    std::set<Id> selected(vertices.begin(), vertices.end());
    if (selected.size() < 2 || selected.size() != vertices.size() || !finite(value))
    {
        return fail(error, "合并需要至少两个不同顶点和有限位置");
    }
    std::map<Id, Id> mapping;
    for (auto id : selected)
    {
        if (!EditableMesh::find_vertex(data, id))
        {
            return fail(error, "顶点选区包含缺失身份");
        }
        mapping[id] = *selected.begin();
    }
    schema::EditableMeshDataT result(data);
    set_position(result, *selected.begin(), value);
    remap_and_clean(result, mapping);
    return publish(data, result, error);
}

bool MeshTopology::split_vertices(schema::EditableMeshDataT& data, std::span<const Id> vertices,
                                  std::string& error)
{
    if (!EditableMesh::validate(data, error))
    {
        return false;
    }
    const std::set<Id> selected(vertices.begin(), vertices.end());
    if (selected.empty() || selected.size() != vertices.size())
    {
        return fail(error, "拆点需要不重复的非空顶点选区");
    }
    for (auto id : selected)
    {
        if (!EditableMesh::find_vertex(data, id))
        {
            return fail(error, "拆点选区包含不存在的顶点");
        }
    }
    schema::EditableMeshDataT result(data);
    std::set<Id> used;
    for (auto& face : result.faces)
    {
        const auto split_ring = [&](auto& ring)
        {
            for (auto& id : ring)
            {
                if (selected.contains(id) && !used.insert(id).second)
                {
                    id = add_vertex(result, position(data, id));
                }
            }
        };
        split_ring(face->vertices);
        for (auto& hole : face->holes)
        {
            split_ring(hole->vertices);
        }
    }
    return publish(data, result, error);
}

bool MeshTopology::detach_faces(schema::EditableMeshDataT& data, std::span<const Id> faces,
                                std::string& error)
{
    std::set<Id> selected;
    if (!face_selection(data, faces, selected, error))
    {
        return false;
    }
    schema::EditableMeshDataT result(data);
    std::map<Id, Id> mapping;
    for (auto id : selected)
    {
        for (auto vertex : face_vertices(*EditableMesh::find_face(data, id)))
        {
            if (!mapping.contains(vertex))
            {
                mapping[vertex] = add_vertex(result, position(data, vertex));
            }
        }
    }
    for (auto id : selected)
    {
        map_face(*EditableMesh::find_face(result, id), mapping);
    }
    remove_orphans(result);
    return publish(data, result, error);
}

bool MeshTopology::duplicate_faces(schema::EditableMeshDataT& data, std::span<const Id> faces,
                                   std::string& error)
{
    std::set<Id> selected;
    if (!face_selection(data, faces, selected, error))
    {
        return false;
    }
    schema::EditableMeshDataT result(data);
    std::map<Id, Id> mapping;
    for (auto id : selected)
    {
        auto copy = std::make_unique<schema::ModelingFaceT>(*EditableMesh::find_face(data, id));
        for (auto vertex : face_vertices(*copy))
        {
            if (!mapping.contains(vertex))
            {
                mapping[vertex] = add_vertex(result, position(data, vertex));
            }
        }
        copy->id = result.next_id++;
        map_face(*copy, mapping);
        result.faces.push_back(std::move(copy));
    }
    return publish(data, result, error);
}

bool MeshTopology::bridge(schema::EditableMeshDataT& data, std::span<const Id> first_loop,
                          std::span<const Id> second_loop, std::string& error)
{
    if (!EditableMesh::validate(data, error) || !boundary_loop(data, first_loop, error) ||
        !boundary_loop(data, second_loop, error))
    {
        return false;
    }
    if (first_loop.size() != second_loop.size())
    {
        return fail(error, "桥接边界环需要相同数量的顶点");
    }
    const std::set<Id> first(first_loop.begin(), first_loop.end());
    for (auto id : second_loop)
    {
        if (first.contains(id))
        {
            return fail(error, "桥接边界环不能共享顶点");
        }
    }
    const auto first_direction = boundary_direction(data, first_loop[0], first_loop[1]);
    const auto second_direction = boundary_direction(data, second_loop[0], second_loop[1]);
    if (first_direction == second_direction)
    {
        return fail(error, "桥接两个环的对应绕序不兼容，请反转其中一个输入环");
    }
    schema::EditableMeshDataT result(data);
    for (std::size_t i = 0; i < first_loop.size(); ++i)
    {
        const auto next = (i + 1) % first_loop.size();
        auto& face =
            add_face(result, {first_loop[i], first_loop[next], second_loop[next], second_loop[i]});
        if (first_direction > 0)
        {
            reverse_face(face);
        }
        // 非共面桥接四边形拆成两个三角形。
        const auto a = position(result, face.vertices[0]);
        const auto b = position(result, face.vertices[1]);
        const auto c = position(result, face.vertices[2]);
        const auto d = position(result, face.vertices[3]);
        if (std::abs(dot(normalized(cross(sub(b, a), sub(c, a))), sub(d, a))) > 1.0e-7)
        {
            const auto ids = face.vertices;
            face.vertices = {ids[0], ids[1], ids[2]};
            add_face(result, {ids[0], ids[2], ids[3]});
        }
    }
    return publish(data, result, error);
}

bool MeshTopology::fill_hole(schema::EditableMeshDataT& data, std::span<const Id> loop,
                             std::string& error)
{
    if (!EditableMesh::validate(data, error) || !boundary_loop(data, loop, error))
    {
        return false;
    }
    schema::EditableMeshDataT result(data);
    auto ring = std::vector<Id>(loop.begin(), loop.end());
    if (boundary_direction(data, ring[0], ring[1]) > 0)
    {
        std::reverse(ring.begin(), ring.end());
    }
    add_face(result, std::move(ring));
    return publish(data, result, error);
}

bool MeshTopology::dissolve_edge(schema::EditableMeshDataT& data, MeshEdge edge, std::string& error)
{
    if (!EditableMesh::validate(data, error))
    {
        return false;
    }
    std::vector<const schema::ModelingFaceT*> adjacent;
    for (const auto& face : data.faces)
    {
        for (std::size_t i = 0; i < face->vertices.size(); ++i)
        {
            if (edge_key(face->vertices[i], face->vertices[(i + 1) % face->vertices.size()]) ==
                edge_key(edge.first, edge.second))
            {
                adjacent.push_back(face.get());
                break;
            }
        }
    }
    if (adjacent.size() != 2 || !adjacent[0]->holes.empty() || !adjacent[1]->holes.empty() ||
        adjacent[0]->material_slot != adjacent[1]->material_slot ||
        adjacent[0]->smoothing_group != adjacent[1]->smoothing_group)
    {
        return fail(error, "溶解需要相同材质和平滑组的两个无洞相邻面");
    }
    const auto effective_uv = [&](const schema::ModelingFaceT& face, std::size_t index)
    {
        if (!face.texcoords.empty())
        {
            return face.texcoords[index];
        }
        const auto value =
            project(position(data, face.vertices[index]), projection_axis(face_normal(data, face)));
        return schema::Vec2(value[0], value[1]);
    };
    for (auto id : {edge.first, edge.second})
    {
        const auto index0 = static_cast<std::size_t>(
            std::find(adjacent[0]->vertices.begin(), adjacent[0]->vertices.end(), id) -
            adjacent[0]->vertices.begin());
        const auto index1 = static_cast<std::size_t>(
            std::find(adjacent[1]->vertices.begin(), adjacent[1]->vertices.end(), id) -
            adjacent[1]->vertices.begin());
        const auto uv0 = effective_uv(*adjacent[0], index0);
        const auto uv1 = effective_uv(*adjacent[1], index1);
        const auto color0 = corner_color(*adjacent[0], index0);
        const auto color1 = corner_color(*adjacent[1], index1);
        if (std::abs(uv0.x() - uv1.x()) > epsilon || std::abs(uv0.y() - uv1.y()) > epsilon ||
            std::abs(color0.r() - color1.r()) > epsilon ||
            std::abs(color0.g() - color1.g()) > epsilon ||
            std::abs(color0.b() - color1.b()) > epsilon ||
            std::abs(color0.a() - color1.a()) > epsilon)
        {
            return fail(error, "溶解将丢失 UV 或颜色接缝，请先统一共享角属性");
        }
    }
    auto merged = std::make_unique<schema::ModelingFaceT>();
    merged->id = adjacent[0]->id;
    merged->material_slot = adjacent[0]->material_slot;
    merged->smoothing_group = adjacent[0]->smoothing_group;
    struct Corner
    {
        Id next = 0;
        schema::Vec2 uv;
        schema::Color color;
    };
    std::map<Id, Corner> boundary;
    for (const auto* face : adjacent)
    {
        for (std::size_t i = 0; i < face->vertices.size(); ++i)
        {
            const auto a = face->vertices[i];
            const auto b = face->vertices[(i + 1) % face->vertices.size()];
            if (edge_key(a, b) == edge_key(edge.first, edge.second))
            {
                continue;
            }
            if (!boundary.emplace(a, Corner{b, effective_uv(*face, i), corner_color(*face, i)})
                     .second)
            {
                return fail(error, "相邻面绕序不一致或共享多条边");
            }
        }
    }
    const auto start = boundary.begin()->first;
    auto current = start;
    do
    {
        const auto found = boundary.find(current);
        if (found == boundary.end() || merged->vertices.size() >= boundary.size())
        {
            return fail(error, "无法重建溶解后的边界");
        }
        merged->vertices.push_back(current);
        merged->texcoords.push_back(found->second.uv);
        merged->colors.push_back(found->second.color);
        current = found->second.next;
    } while (current != start);
    if (merged->vertices.size() != boundary.size())
    {
        return fail(error, "溶解产生多个边界环");
    }
    schema::EditableMeshDataT result(data);
    std::erase_if(result.faces, [&](const auto& face)
                  { return face->id == adjacent[0]->id || face->id == adjacent[1]->id; });
    result.faces.push_back(std::move(merged));
    return publish(data, result, error);
}

bool MeshTopology::split_edge(schema::EditableMeshDataT& data, MeshEdge edge, double fraction,
                              std::string& error)
{
    if (!EditableMesh::validate(data, error))
    {
        return false;
    }
    if (!std::isfinite(fraction) || fraction <= epsilon || fraction >= 1.0 - epsilon ||
        edge.first == edge.second || !EditableMesh::find_vertex(data, edge.first) ||
        !EditableMesh::find_vertex(data, edge.second))
    {
        return fail(error, "分边比例必须在 0 和 1 之间，端点必须存在且不同");
    }
    schema::EditableMeshDataT result(data);
    const auto id = add_vertex(result, add(mul(position(data, edge.first), 1.0 - fraction),
                                           mul(position(data, edge.second), fraction)));
    bool found = false;
    const auto split = [&](auto& ids, auto& uv, auto& colors)
    {
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            const auto next = (i + 1) % ids.size();
            if (edge_key(ids[i], ids[next]) != edge_key(edge.first, edge.second))
            {
                continue;
            }
            const auto t = ids[i] == edge.first ? fraction : 1.0 - fraction;
            if (!uv.empty())
            {
                uv.insert(uv.begin() + static_cast<std::ptrdiff_t>(i + 1), mix(uv[i], uv[next], t));
            }
            if (!colors.empty())
            {
                colors.insert(colors.begin() + static_cast<std::ptrdiff_t>(i + 1),
                              mix(colors[i], colors[next], t));
            }
            ids.insert(ids.begin() + static_cast<std::ptrdiff_t>(i + 1), id);
            found = true;
            break;
        }
    };
    for (auto& face : result.faces)
    {
        split(face->vertices, face->texcoords, face->colors);
        for (auto& hole : face->holes)
        {
            split(hole->vertices, hole->texcoords, hole->colors);
        }
    }
    if (!found)
    {
        return fail(error, "目标边不存在");
    }
    return publish(data, result, error);
}

bool MeshTopology::connect_vertices(schema::EditableMeshDataT& data, Id face_id, Id first,
                                    Id second, std::string& error)
{
    if (!EditableMesh::validate(data, error))
    {
        return false;
    }
    const auto* face = EditableMesh::find_face(data, face_id);
    if (!face || !face->holes.empty() || first == second)
    {
        return fail(error, "连接操作需要无洞面上的两个不同顶点");
    }
    const auto first_it = std::find(face->vertices.begin(), face->vertices.end(), first);
    const auto second_it = std::find(face->vertices.begin(), face->vertices.end(), second);
    if (first_it == face->vertices.end() || second_it == face->vertices.end())
    {
        return fail(error, "连接顶点不属于目标面");
    }
    const auto a = static_cast<std::size_t>(first_it - face->vertices.begin());
    const auto b = static_cast<std::size_t>(second_it - face->vertices.begin());
    if ((a + 1) % face->vertices.size() == b || (b + 1) % face->vertices.size() == a)
    {
        return fail(error, "连接顶点已经相邻");
    }
    schema::EditableMeshDataT result(data);
    const auto make_part = [&](std::size_t start, std::size_t end, Id id)
    {
        auto part = std::make_unique<schema::ModelingFaceT>();
        part->id = id;
        part->material_slot = face->material_slot;
        part->smoothing_group = face->smoothing_group;
        auto i = start;
        for (;;)
        {
            part->vertices.push_back(face->vertices[i]);
            if (!face->texcoords.empty())
            {
                part->texcoords.push_back(face->texcoords[i]);
            }
            if (!face->colors.empty())
            {
                part->colors.push_back(face->colors[i]);
            }
            if (i == end)
            {
                break;
            }
            i = (i + 1) % face->vertices.size();
        }
        return part;
    };
    auto part0 = make_part(a, b, face_id);
    auto part1 = make_part(b, a, result.next_id++);
    const auto original_normal = face_normal(data, *face);
    if (dot(face_normal(result, *part0), original_normal) <= epsilon ||
        dot(face_normal(result, *part1), original_normal) <= epsilon)
    {
        return fail(error, "连接线离开了多边形内部");
    }
    std::erase_if(result.faces, [face_id](const auto& item) { return item->id == face_id; });
    result.faces.push_back(std::move(part0));
    result.faces.push_back(std::move(part1));
    return publish(data, result, error);
}

bool MeshTopology::append(schema::EditableMeshDataT& data, const schema::EditableMeshDataT& other,
                          std::string& error, std::uint32_t material_slot_offset)
{
    if (!EditableMesh::validate(data, error) || !EditableMesh::validate(other, error))
    {
        return false;
    }
    schema::EditableMeshDataT result(data);
    std::map<Id, Id> mapping;
    for (const auto& vertex : other.vertices)
    {
        mapping[vertex.id()] = add_vertex(result, position(vertex));
    }
    for (const auto& face : other.faces)
    {
        auto copy = std::make_unique<schema::ModelingFaceT>(*face);
        copy->id = result.next_id++;
        if (material_slot_offset > 15 || copy->material_slot > 15 - material_slot_offset)
        {
            return fail(error, "合并后的材质槽超过 16 槽限制");
        }
        copy->material_slot += material_slot_offset;
        map_face(*copy, mapping);
        result.faces.push_back(std::move(copy));
    }
    return publish(data, result, error);
}

bool MeshTopology::set_material(schema::EditableMeshDataT& data, std::span<const Id> faces,
                                std::uint32_t slot, std::string& error)
{
    std::set<Id> selected;
    if (!face_selection(data, faces, selected, error))
    {
        return false;
    }
    if (slot >= 16)
    {
        return fail(error, "材质槽必须在 0 到 15 之间");
    }
    schema::EditableMeshDataT result(data);
    for (auto id : selected)
    {
        EditableMesh::find_face(result, id)->material_slot = slot;
    }
    return publish(data, result, error);
}

bool MeshTopology::set_color(schema::EditableMeshDataT& data, std::span<const Id> faces,
                             const schema::Color& color, std::string& error)
{
    std::set<Id> selected;
    if (!face_selection(data, faces, selected, error))
    {
        return false;
    }
    for (auto value : {color.r(), color.g(), color.b(), color.a()})
    {
        if (!std::isfinite(value) || value < 0 || value > 1)
        {
            return fail(error, "颜色分量必须在 0 到 1 之间");
        }
    }
    schema::EditableMeshDataT result(data);
    for (auto id : selected)
    {
        auto* face = EditableMesh::find_face(result, id);
        face->colors.assign(face->vertices.size(), color);
        for (auto& hole : face->holes)
        {
            hole->colors.assign(hole->vertices.size(), color);
        }
    }
    return publish(data, result, error);
}

bool MeshTopology::select_faces(const schema::EditableMeshDataT& data, std::span<const Id> faces,
                                MeshFaceSelection mode, std::vector<Id>& output, std::string& error)
{
    output.clear();
    std::set<Id> selected;
    if (!face_selection(data, faces, selected, error))
    {
        return false;
    }
    std::map<EdgeKey, std::vector<Id>> edge_faces;
    const auto add_ring = [&](const auto& ring, Id face)
    {
        for (std::size_t i = 0; i < ring.size(); ++i)
        {
            edge_faces[edge_key(ring[i], ring[(i + 1) % ring.size()])].push_back(face);
        }
    };
    for (const auto& face : data.faces)
    {
        add_ring(face->vertices, face->id);
        for (const auto& hole : face->holes)
        {
            add_ring(hole->vertices, face->id);
        }
    }
    std::map<Id, std::set<Id>> neighbors;
    std::set<Id> boundary;
    for (const auto& [edge, incident] : edge_faces)
    {
        (void)edge;
        if (incident.size() == 1)
        {
            boundary.insert(incident.front());
        }
        for (auto face : incident)
        {
            for (auto adjacent : incident)
            {
                if (face != adjacent)
                {
                    neighbors[face].insert(adjacent);
                }
            }
        }
    }
    auto result = selected;
    if (mode == MeshFaceSelection::shrink)
    {
        for (auto face : selected)
        {
            if (boundary.contains(face) ||
                std::any_of(neighbors[face].begin(), neighbors[face].end(),
                            [&](auto adjacent) { return !selected.contains(adjacent); }))
            {
                result.erase(face);
            }
        }
    }
    else if (mode == MeshFaceSelection::grow || mode == MeshFaceSelection::connected)
    {
        std::vector<Id> pending(selected.begin(), selected.end());
        for (std::size_t i = 0; i < pending.size(); ++i)
        {
            for (auto adjacent : neighbors[pending[i]])
            {
                if (result.insert(adjacent).second && mode == MeshFaceSelection::connected)
                {
                    pending.push_back(adjacent);
                }
            }
        }
    }
    else
    {
        return fail(error, "面选择扩展模式未知");
    }
    output.assign(result.begin(), result.end());
    return true;
}

bool MeshTopology::edge_ring(const schema::EditableMeshDataT& data, MeshEdge edge,
                             std::vector<MeshEdge>& output, std::string& error)
{
    output.clear();
    if (!EditableMesh::validate(data, error))
    {
        return false;
    }
    std::map<EdgeKey, std::vector<const schema::ModelingFaceT*>> adjacent;
    for (const auto& face : data.faces)
    {
        for (std::size_t i = 0; i < face->vertices.size(); ++i)
        {
            adjacent[edge_key(face->vertices[i], face->vertices[(i + 1) % face->vertices.size()])]
                .push_back(face.get());
        }
    }
    const auto start = edge_key(edge.first, edge.second);
    if (!adjacent.contains(start))
    {
        return fail(error, "起始边不存在");
    }
    std::set<EdgeKey> visited{start};
    std::vector<EdgeKey> pending{start};
    for (std::size_t index = 0; index < pending.size(); ++index)
    {
        const auto current = pending[index];
        if (adjacent[current].size() > 2)
        {
            return fail(error, "边环遇到非流形边");
        }
        for (const auto* face : adjacent[current])
        {
            if (face->vertices.size() != 4 || !face->holes.empty())
            {
                continue;
            }
            for (std::size_t i = 0; i < 4; ++i)
            {
                if (edge_key(face->vertices[i], face->vertices[(i + 1) % 4]) == current)
                {
                    const auto opposite =
                        edge_key(face->vertices[(i + 2) % 4], face->vertices[(i + 3) % 4]);
                    if (visited.insert(opposite).second)
                    {
                        pending.push_back(opposite);
                    }
                    break;
                }
            }
        }
    }
    for (const auto& value : pending)
    {
        output.push_back({value.first, value.second});
    }
    return true;
}

bool MeshTopology::edge_loop(const schema::EditableMeshDataT& data, MeshEdge edge,
                             std::vector<MeshEdge>& output, std::string& error)
{
    output.clear();
    if (!EditableMesh::validate(data, error))
    {
        return false;
    }
    std::map<EdgeKey, std::set<Id>> edge_faces;
    std::map<Id, std::set<EdgeKey>> vertex_edges;
    for (const auto& face : data.faces)
    {
        const auto append_ring = [&](const auto& ring)
        {
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                const auto key = edge_key(ring[i], ring[(i + 1) % ring.size()]);
                edge_faces[key].insert(face->id);
                vertex_edges[key.first].insert(key);
                vertex_edges[key.second].insert(key);
            }
        };
        append_ring(face->vertices);
        for (const auto& hole : face->holes)
        {
            append_ring(hole->vertices);
        }
    }
    const auto start = edge_key(edge.first, edge.second);
    if (!edge_faces.contains(start))
    {
        return fail(error, "边循环的起始边不存在");
    }
    std::vector<EdgeKey> pending{start};
    std::set<EdgeKey> visited{start};
    for (std::size_t i = 0; i < pending.size(); ++i)
    {
        const auto current = pending[i];
        if (edge_faces[current].size() > 2)
        {
            return fail(error, "边循环遇到非流形边");
        }
        for (auto vertex : {current.first, current.second})
        {
            std::vector<EdgeKey> candidates;
            for (const auto& next : vertex_edges[vertex])
            {
                if (next == current)
                {
                    continue;
                }
                const bool same_face =
                    std::any_of(edge_faces[current].begin(), edge_faces[current].end(),
                                [&](auto face) { return edge_faces[next].contains(face); });
                if (!same_face)
                {
                    candidates.push_back(next);
                }
            }
            if (candidates.size() > 1)
            {
                return fail(error, "边循环在非常规顶点处分叉，无法唯一继续");
            }
            if (!candidates.empty() && visited.insert(candidates.front()).second)
            {
                pending.push_back(candidates.front());
            }
        }
    }
    for (const auto& value : pending)
    {
        output.push_back({value.first, value.second});
    }
    return true;
}

bool MeshTopology::loop_cut(schema::EditableMeshDataT& data, MeshEdge edge, std::uint32_t cuts,
                            double pinch, double offset, std::string& error)
{
    if (!EditableMesh::validate(data, error))
    {
        return false;
    }
    if (cuts == 0 || cuts > 64 || !std::isfinite(pinch) || !std::isfinite(offset) ||
        pinch <= -1.0 || pinch >= 1.0)
    {
        return fail(error, "环切需要 1 到 64 条切线，pinch 必须在 -1 和 1 之间");
    }
    std::vector<double> fractions;
    for (std::uint32_t i = 0; i < cuts; ++i)
    {
        const auto value =
            0.5 + (static_cast<double>(i + 1) / (cuts + 1) - 0.5) * (1.0 - pinch) + offset;
        if (value <= epsilon || value >= 1.0 - epsilon)
        {
            return fail(error, "环切间距或偏移将切线推到边界之外");
        }
        fractions.push_back(value);
    }
    std::map<EdgeKey, std::vector<const schema::ModelingFaceT*>> adjacent;
    for (const auto& face : data.faces)
    {
        const auto append_ring = [&](const auto& ring)
        {
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                adjacent[edge_key(ring[i], ring[(i + 1) % ring.size()])].push_back(face.get());
            }
        };
        append_ring(face->vertices);
        for (const auto& hole : face->holes)
        {
            append_ring(hole->vertices);
        }
    }
    const auto start = edge_key(edge.first, edge.second);
    if (!adjacent.contains(start))
    {
        return fail(error, "环切起始边不存在");
    }
    std::map<EdgeKey, MeshEdge> directions{{start, edge}};
    std::vector<EdgeKey> pending{start};
    std::map<Id, std::size_t> affected;
    for (std::size_t index = 0; index < pending.size(); ++index)
    {
        const auto current = pending[index];
        if (adjacent[current].size() > 2)
        {
            return fail(error, "环切遇到非流形边");
        }
        const auto direction = directions[current];
        for (const auto* face : adjacent[current])
        {
            if (face->vertices.size() != 4 || !face->holes.empty())
            {
                continue;
            }
            for (std::size_t i = 0; i < 4; ++i)
            {
                if (edge_key(face->vertices[i], face->vertices[(i + 1) % 4]) != current)
                {
                    continue;
                }
                const bool forward = face->vertices[i] == direction.first;
                const MeshEdge opposite =
                    forward ? MeshEdge{face->vertices[(i + 3) % 4], face->vertices[(i + 2) % 4]}
                            : MeshEdge{face->vertices[(i + 2) % 4], face->vertices[(i + 3) % 4]};
                const auto key = edge_key(opposite.first, opposite.second);
                const auto [found, inserted] = directions.emplace(key, opposite);
                if (!inserted && found->second.first != opposite.first)
                {
                    return fail(error, "环切遇到方向不一致的扭转四边形带");
                }
                if (inserted)
                {
                    pending.push_back(key);
                }
                affected.try_emplace(face->id, i);
                break;
            }
        }
    }
    if (affected.empty())
    {
        return fail(error, "环切需要至少一个无洞四边形");
    }
    schema::EditableMeshDataT result(data);
    std::map<EdgeKey, std::vector<Id>> inserted;
    for (const auto& [key, direction] : directions)
    {
        auto& points = inserted[key];
        for (auto t : fractions)
        {
            points.push_back(add_vertex(result, add(mul(position(data, direction.first), 1.0 - t),
                                                    mul(position(data, direction.second), t))));
        }
    }
    std::erase_if(result.faces, [&](const auto& face) { return affected.contains(face->id); });
    for (const auto& [id, side] : affected)
    {
        const auto* original = EditableMesh::find_face(data, id);
        const auto a = original->vertices[side];
        const auto b = original->vertices[(side + 1) % 4];
        const auto c = original->vertices[(side + 2) % 4];
        const auto d = original->vertices[(side + 3) % 4];
        const auto first_key = edge_key(a, b);
        const auto second_key = edge_key(d, c);
        auto near = inserted[first_key];
        auto far = inserted[second_key];
        auto t_values = fractions;
        if (directions[first_key].first != a)
        {
            std::reverse(near.begin(), near.end());
            std::reverse(t_values.begin(), t_values.end());
            for (auto& value : t_values)
            {
                value = 1.0 - value;
            }
        }
        if (directions[second_key].first != d)
        {
            std::reverse(far.begin(), far.end());
        }
        near.insert(near.begin(), a);
        near.push_back(b);
        far.insert(far.begin(), d);
        far.push_back(c);
        t_values.insert(t_values.begin(), 0.0);
        t_values.push_back(1.0);
        for (std::size_t i = 0; i + 1 < near.size(); ++i)
        {
            auto face = std::make_unique<schema::ModelingFaceT>();
            face->id = i == 0 ? id : result.next_id++;
            face->vertices = {near[i], near[i + 1], far[i + 1], far[i]};
            face->material_slot = original->material_slot;
            face->smoothing_group = original->smoothing_group;
            if (!original->texcoords.empty())
            {
                const auto& uv = original->texcoords;
                face->texcoords = {mix(uv[side], uv[(side + 1) % 4], t_values[i]),
                                   mix(uv[side], uv[(side + 1) % 4], t_values[i + 1]),
                                   mix(uv[(side + 3) % 4], uv[(side + 2) % 4], t_values[i + 1]),
                                   mix(uv[(side + 3) % 4], uv[(side + 2) % 4], t_values[i])};
            }
            if (!original->colors.empty())
            {
                const auto& colors = original->colors;
                face->colors = {
                    mix(colors[side], colors[(side + 1) % 4], t_values[i]),
                    mix(colors[side], colors[(side + 1) % 4], t_values[i + 1]),
                    mix(colors[(side + 3) % 4], colors[(side + 2) % 4], t_values[i + 1]),
                    mix(colors[(side + 3) % 4], colors[(side + 2) % 4], t_values[i])};
            }
            result.faces.push_back(std::move(face));
        }
    }
    // 四边形带终点的非四边形邻面只插入共享边顶点，保持封闭。
    for (auto& face : result.faces)
    {
        if (affected.contains(face->id) || !EditableMesh::find_face(data, face->id))
        {
            continue;
        }
        const auto insert_ring = [&](auto& ids, auto& uv, auto& colors)
        {
            const auto old_ids = ids;
            const auto old_uv = uv;
            const auto old_colors = colors;
            ids.clear();
            uv.clear();
            colors.clear();
            for (std::size_t i = 0; i < old_ids.size(); ++i)
            {
                const auto next = (i + 1) % old_ids.size();
                ids.push_back(old_ids[i]);
                if (!old_uv.empty())
                {
                    uv.push_back(old_uv[i]);
                }
                if (!old_colors.empty())
                {
                    colors.push_back(old_colors[i]);
                }
                const auto key = edge_key(old_ids[i], old_ids[next]);
                const auto found = inserted.find(key);
                if (found == inserted.end())
                {
                    continue;
                }
                const bool forward = directions[key].first == old_ids[i];
                for (std::size_t j = 0; j < found->second.size(); ++j)
                {
                    const auto index = forward ? j : found->second.size() - 1 - j;
                    const auto t = forward ? fractions[index] : 1.0 - fractions[index];
                    ids.push_back(found->second[index]);
                    if (!old_uv.empty())
                    {
                        uv.push_back(mix(old_uv[i], old_uv[next], t));
                    }
                    if (!old_colors.empty())
                    {
                        colors.push_back(mix(old_colors[i], old_colors[next], t));
                    }
                }
            }
        };
        insert_ring(face->vertices, face->texcoords, face->colors);
        for (auto& hole : face->holes)
        {
            insert_ring(hole->vertices, hole->texcoords, hole->colors);
        }
    }
    return publish(data, result, error);
}

} // namespace engine
