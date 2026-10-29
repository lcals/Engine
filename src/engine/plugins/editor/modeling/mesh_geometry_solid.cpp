#include "plugins/editor/modeling/mesh_geometry.hpp"

#include "plugins/editor/modeling/mesh_geometry_internal.hpp"

#include <manifold/manifold.h>

#include <array>
#include <numeric>

namespace engine
{
namespace
{

using namespace modeling_detail;

struct FaceAttributes
{
    std::uint32_t material = 0;
    std::uint32_t smoothing = 0;
};

using Provenance = std::map<std::uint32_t, FaceAttributes>;

struct Corner
{
    std::uint64_t vertex = 0;
    schema::Vec2 uv;
    schema::Color color = {1.0F, 1.0F, 1.0F, 1.0F};
};

std::vector<Corner> corners(const schema::ModelingFaceT& face)
{
    std::vector<Corner> result;
    const auto append = [&](const auto& vertices, const auto& texcoords, const auto& colors)
    {
        for (std::size_t i = 0; i < vertices.size(); ++i)
        {
            Corner corner;
            corner.vertex = vertices[i];
            if (!texcoords.empty())
            {
                corner.uv = texcoords[i];
            }
            if (!colors.empty())
            {
                corner.color = colors[i];
            }
            result.push_back(corner);
        }
    };
    append(face.vertices, face.texcoords, face.colors);
    for (const auto& hole : face.holes)
    {
        append(hole->vertices, hole->texcoords, hole->colors);
    }
    return result;
}

bool status_ok(const manifold::Manifold& solid, std::string& error)
{
    const auto status = solid.Status();
    if (status != manifold::Manifold::Error::NoError)
    {
        error = "Solid operation failed with Manifold error code " +
                std::to_string(static_cast<int>(status));
        return false;
    }
    return true;
}

bool to_solid(const schema::EditableMeshDataT& data, manifold::Manifold& output,
              Provenance& provenance, std::string& error)
{
    if (!EditableMesh::validate(data, error))
    {
        return false;
    }
    if (data.faces.empty())
    {
        output = manifold::Manifold();
        return true;
    }
    manifold::MeshGL64 mesh;
    mesh.numProp = 9;
    std::map<std::uint64_t, std::uint64_t> first_corner;
    const auto first_original =
        manifold::Manifold::ReserveIDs(static_cast<std::uint32_t>(data.faces.size()));
    for (std::size_t f = 0; f < data.faces.size(); ++f)
    {
        const auto& face = *data.faces[f];
        std::vector<std::uint32_t> triangles;
        if (!EditableMesh::triangulate_face(data, face, triangles, error))
        {
            return false;
        }
        const auto source = corners(face);
        const auto first = mesh.NumVert();
        for (const auto& corner : source)
        {
            const Point p = position(data, corner.vertex);
            const auto index = mesh.NumVert();
            const auto [found, inserted] = first_corner.emplace(corner.vertex, index);
            if (!inserted)
            {
                mesh.mergeFromVert.push_back(index);
                mesh.mergeToVert.push_back(found->second);
            }
            const auto& color = corner.color;
            mesh.vertProperties.insert(mesh.vertProperties.end(),
                                       {p.x, p.y, p.z, corner.uv.x(), corner.uv.y(), color.r(),
                                        color.g(), color.b(), color.a()});
        }
        const auto original = first_original + static_cast<std::uint32_t>(f);
        provenance.emplace(original, FaceAttributes{face.material_slot, face.smoothing_group});
        mesh.runIndex.push_back(mesh.triVerts.size());
        mesh.runOriginalID.push_back(original);
        for (const auto index : triangles)
        {
            mesh.triVerts.push_back(first + index);
        }
        for (std::size_t i = 0; i < triangles.size(); i += 3)
        {
            mesh.faceID.push_back(face.id);
        }
    }
    mesh.runIndex.push_back(mesh.triVerts.size());
    output = manifold::Manifold(mesh);
    return status_ok(output, error);
}

bool from_solid(const manifold::Manifold& solid, const Provenance& provenance,
                std::uint32_t cap_material, std::uint64_t next_id,
                schema::EditableMeshDataT& output, std::string& error)
{
    if (!status_ok(solid, error))
    {
        return false;
    }
    const auto mesh = solid.GetMeshGL64();
    schema::EditableMeshDataT candidate;
    candidate.next_id = next_id;
    if (mesh.NumTri() == 0)
    {
        return commit(output, std::move(candidate), error);
    }
    std::vector<std::uint64_t> parent(mesh.NumVert());
    std::iota(parent.begin(), parent.end(), 0);
    const auto root = [&](std::uint64_t index)
    {
        while (parent[index] != index)
        {
            index = parent[index];
        }
        return index;
    };
    for (std::size_t i = 0; i < mesh.mergeFromVert.size(); ++i)
    {
        const auto a = root(mesh.mergeFromVert[i]);
        const auto b = root(mesh.mergeToVert[i]);
        parent[a] = b;
    }
    std::map<std::uint64_t, std::uint64_t> vertex_ids;
    std::vector<std::uint64_t> remap(mesh.NumVert());
    for (std::uint64_t i = 0; i < mesh.NumVert(); ++i)
    {
        const auto canonical = root(i);
        const auto found = vertex_ids.find(canonical);
        if (found != vertex_ids.end())
        {
            remap[i] = found->second;
            continue;
        }
        const auto offset = i * mesh.numProp;
        const auto id =
            add_vertex(candidate, {mesh.vertProperties[offset], mesh.vertProperties[offset + 1],
                                   mesh.vertProperties[offset + 2]});
        vertex_ids.emplace(canonical, id);
        remap[i] = id;
    }
    std::size_t run = 0;
    for (std::size_t i = 0; i < mesh.triVerts.size(); i += 3)
    {
        while (run + 1 < mesh.runOriginalID.size() && i >= mesh.runIndex[run + 1])
        {
            ++run;
        }
        auto face = std::make_unique<schema::ModelingFaceT>();
        face->id = candidate.next_id++;
        face->material_slot = cap_material;
        bool original_face = false;
        if (run < mesh.runOriginalID.size())
        {
            const auto attributes = provenance.find(mesh.runOriginalID[run]);
            if (attributes != provenance.end())
            {
                face->material_slot = attributes->second.material;
                face->smoothing_group = attributes->second.smoothing;
                original_face = true;
            }
        }
        for (std::size_t j = 0; j < 3; ++j)
        {
            const auto index = mesh.triVerts[i + j];
            const auto offset = index * mesh.numProp;
            face->vertices.push_back(remap[index]);
            if (mesh.numProp >= 9 && original_face)
            {
                face->texcoords.emplace_back(mesh.vertProperties[offset + 3],
                                             mesh.vertProperties[offset + 4]);
                face->colors.emplace_back(static_cast<float>(mesh.vertProperties[offset + 5]),
                                          static_cast<float>(mesh.vertProperties[offset + 6]),
                                          static_cast<float>(mesh.vertProperties[offset + 7]),
                                          static_cast<float>(mesh.vertProperties[offset + 8]));
            }
        }
        if (!original_face)
        {
            Point normal;
            if (!face_normal(candidate, *face, normal))
            {
                error = "Solid result contains a degenerate cap";
                return false;
            }
            Point u;
            Point v;
            basis(normal, u, v);
            for (const auto id : face->vertices)
            {
                const auto p = position(candidate, id);
                face->texcoords.emplace_back(dot(p, u), dot(p, v));
            }
        }
        candidate.faces.push_back(std::move(face));
    }
    remove_unused_vertices(candidate);
    return commit(output, std::move(candidate), error);
}

bool clip_open(schema::EditableMeshDataT& data, Point normal, double offset, std::string& error)
{
    schema::EditableMeshDataT candidate;
    candidate.vertices = data.vertices;
    candidate.next_id = data.next_id;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::uint64_t> intersections;
    const auto intersection = [&](const Corner& a, const Corner& b)
    {
        const Point pa = position(candidate, a.vertex);
        const Point pb = position(candidate, b.vertex);
        const double da = dot(normal, pa) - offset;
        const double db = dot(normal, pb) - offset;
        const double t = std::clamp(da / (da - db), 0.0, 1.0);
        Corner result;
        if (t <= epsilon)
        {
            return a;
        }
        if (t >= 1.0 - epsilon)
        {
            return b;
        }
        const auto key = edge_key(a.vertex, b.vertex);
        const auto found = intersections.find(key);
        if (found == intersections.end())
        {
            result.vertex = add_vertex(candidate, pa + (pb - pa) * t);
            intersections.emplace(key, result.vertex);
        }
        else
        {
            result.vertex = found->second;
        }
        result.uv = schema::Vec2(a.uv.x() + (b.uv.x() - a.uv.x()) * t,
                                 a.uv.y() + (b.uv.y() - a.uv.y()) * t);
        result.color =
            schema::Color(static_cast<float>(a.color.r() + (b.color.r() - a.color.r()) * t),
                          static_cast<float>(a.color.g() + (b.color.g() - a.color.g()) * t),
                          static_cast<float>(a.color.b() + (b.color.b() - a.color.b()) * t),
                          static_cast<float>(a.color.a() + (b.color.a() - a.color.a()) * t));
        return result;
    };
    for (const auto& face : data.faces)
    {
        const auto source = corners(*face);
        std::vector<std::uint32_t> triangles;
        if (!EditableMesh::triangulate_face(data, *face, triangles, error))
        {
            return false;
        }
        for (std::size_t i = 0; i < triangles.size(); i += 3)
        {
            std::vector<Corner> polygon;
            for (std::size_t j = 0; j < 3; ++j)
            {
                const auto& a = source[triangles[i + j]];
                const auto& b = source[triangles[i + (j + 1) % 3]];
                const bool a_inside = dot(normal, position(candidate, a.vertex)) >= offset;
                const bool b_inside = dot(normal, position(candidate, b.vertex)) >= offset;
                if (a_inside)
                {
                    polygon.push_back(a);
                }
                if (a_inside != b_inside)
                {
                    polygon.push_back(intersection(a, b));
                }
            }
            polygon.erase(std::unique(polygon.begin(), polygon.end(),
                                      [](const auto& a, const auto& b)
                                      { return a.vertex == b.vertex; }),
                          polygon.end());
            if (polygon.size() > 1 && polygon.front().vertex == polygon.back().vertex)
            {
                polygon.pop_back();
            }
            if (polygon.size() < 3)
            {
                continue;
            }
            auto clipped = std::make_unique<schema::ModelingFaceT>();
            clipped->id = candidate.next_id++;
            clipped->material_slot = face->material_slot;
            clipped->smoothing_group = face->smoothing_group;
            for (const auto& corner : polygon)
            {
                clipped->vertices.push_back(corner.vertex);
                clipped->texcoords.push_back(corner.uv);
                clipped->colors.push_back(corner.color);
            }
            candidate.faces.push_back(std::move(clipped));
        }
    }
    remove_unused_vertices(candidate);
    return commit(data, std::move(candidate), error);
}

} // namespace

bool MeshGeometry::boolean_operation(schema::EditableMeshDataT& data,
                                     const schema::EditableMeshDataT& other,
                                     MeshBooleanOperation operation, std::string& error)
{
    manifold::Manifold first;
    manifold::Manifold second;
    Provenance provenance;
    if (!to_solid(data, first, provenance, error) || !to_solid(other, second, provenance, error))
    {
        return false;
    }
    manifold::OpType type;
    switch (operation)
    {
    case MeshBooleanOperation::unite:
        type = manifold::OpType::Add;
        break;
    case MeshBooleanOperation::subtract:
        type = manifold::OpType::Subtract;
        break;
    case MeshBooleanOperation::intersect:
        type = manifold::OpType::Intersect;
        break;
    default:
        error = "Unknown solid boolean operation";
        return false;
    }
    return from_solid(first.Boolean(second, type), provenance, 0,
                      std::max(data.next_id, other.next_id), data, error);
}

bool MeshGeometry::clip(schema::EditableMeshDataT& data, const MeshClipOptions& options,
                        std::string& error)
{
    error.clear();
    Point normal = point(options.normal);
    const double size = length(normal);
    if (!normalize(normal) || !std::isfinite(options.offset) ||
        !EditableMesh::validate(data, error))
    {
        if (error.empty())
        {
            error = "Invalid clipping plane";
        }
        return false;
    }
    double offset = options.offset / size;
    if (!options.keep_positive)
    {
        normal = normal * -1.0;
        offset = -offset;
    }
    if (!options.cap)
    {
        return clip_open(data, normal, offset, error);
    }
    manifold::Manifold solid;
    Provenance provenance;
    if (!to_solid(data, solid, provenance, error))
    {
        return false;
    }
    return from_solid(solid.TrimByPlane({normal.x, normal.y, normal.z}, offset), provenance,
                      options.cap_material_slot, data.next_id, data, error);
}

bool MeshGeometry::bevel(schema::EditableMeshDataT& data, const MeshBevelOptions& options,
                         std::string& error)
{
    error.clear();
    if (options.edges.empty() || options.edges.size() > 4096 || !std::isfinite(options.width) ||
        options.width <= 0.0 || options.segments == 0 || options.segments > 64 ||
        !std::isfinite(options.profile) || options.profile <= 0.0 || options.profile >= 1.0 ||
        !EditableMesh::validate(data, error))
    {
        if (error.empty())
        {
            error = "Bevel requires edges, positive width, 1..64 segments, and profile in (0, 1)";
        }
        return false;
    }
    struct Side
    {
        Point normal;
        std::uint64_t start = 0;
        std::uint64_t end = 0;
    };
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<Side>> edges;
    for (const auto& face : data.faces)
    {
        Point normal;
        if (!face->holes.empty() || !face_normal(data, *face, normal))
        {
            error = "Bevel currently requires a convex polyhedron without face holes";
            return false;
        }
        const Point origin = position(data, face->vertices.front());
        for (const auto& vertex : data.vertices)
        {
            if (dot(normal, point(vertex) - origin) > epsilon * 100.0)
            {
                error = "Selected-edge bevel currently requires a convex polyhedron";
                return false;
            }
        }
        for (std::size_t i = 0; i < face->vertices.size(); ++i)
        {
            const auto a = face->vertices[i];
            const auto b = face->vertices[(i + 1) % face->vertices.size()];
            edges[edge_key(a, b)].push_back({normal, a, b});
        }
    }
    for (const auto& [key, sides] : edges)
    {
        (void)key;
        if (sides.size() != 2 || sides[0].start != sides[1].end)
        {
            error = "Bevel requires closed and consistently oriented edge adjacency";
            return false;
        }
    }
    struct Plane
    {
        Point normal;
        double offset = 0.0;
    };
    std::vector<Plane> planes;
    std::set<std::pair<std::uint64_t, std::uint64_t>> selected;
    for (const auto& edge : options.edges)
    {
        const auto key = edge_key(edge.first, edge.second);
        const auto found = edges.find(key);
        if (found == edges.end() || !selected.insert(key).second)
        {
            error = "Bevel selection contains a missing or repeated topology edge";
            return false;
        }
        double limit = std::numeric_limits<double>::max();
        for (const auto& [neighbor, sides] : edges)
        {
            (void)sides;
            if (neighbor == key || (neighbor.first != key.first && neighbor.first != key.second &&
                                    neighbor.second != key.first && neighbor.second != key.second))
            {
                continue;
            }
            limit = std::min(
                limit,
                length(position(data, neighbor.first) - position(data, neighbor.second)) * 0.5);
        }
        if (options.width >= limit)
        {
            error = "Bevel width reaches an adjacent edge; reduce width to avoid overlap";
            return false;
        }
        const auto& sides = found->second;
        const Point n0 = sides[0].normal;
        const Point n1 = sides[1].normal;
        const double cosine = std::clamp(dot(n0, n1), -1.0, 1.0);
        const double sine = std::sqrt(std::max(0.0, 1.0 - cosine * cosine));
        if (sine <= epsilon)
        {
            error = "Bevel requires a convex crease, not a coplanar triangulation edge";
            return false;
        }
        const Point origin = position(data, edge.first);
        Point direction = position(data, edge.second) - origin;
        if (!normalize(direction))
        {
            error = "Bevel edge has no length";
            return false;
        }
        const Point first = origin + (n0 * cosine - n1) * (options.width / sine);
        const Point last = origin + (n1 * cosine - n0) * (options.width / sine);
        const double weight =
            std::sqrt((1.0 + cosine) * 0.5) * options.profile / (1.0 - options.profile);
        const auto curve = [&](double t)
        {
            const double a = (1.0 - t) * (1.0 - t);
            const double b = 2.0 * t * (1.0 - t) * weight;
            const double c = t * t;
            return (first * a + origin * b + last * c) * (1.0 / (a + b + c));
        };
        for (std::uint32_t i = 0; i < options.segments; ++i)
        {
            const Point a = curve(static_cast<double>(i) / options.segments);
            const Point b = curve(static_cast<double>(i + 1) / options.segments);
            Point normal = cross(direction, b - a);
            if (!normalize(normal))
            {
                error = "Bevel profile produced a degenerate segment";
                return false;
            }
            if (dot(normal, n0 + n1) < 0.0)
            {
                normal = normal * -1.0;
            }
            planes.push_back({normal, dot(normal, a)});
        }
    }
    manifold::Manifold solid;
    Provenance provenance;
    if (!to_solid(data, solid, provenance, error))
    {
        return false;
    }
    const double before = solid.Volume();
    for (const auto& plane : planes)
    {
        solid =
            solid.TrimByPlane({-plane.normal.x, -plane.normal.y, -plane.normal.z}, -plane.offset);
        if (!status_ok(solid, error))
        {
            return false;
        }
    }
    const double after = solid.Volume();
    if (after <= epsilon || after >= before || !std::isfinite(after))
    {
        error = "Bevel did not produce a nonempty smaller solid";
        return false;
    }
    return from_solid(solid, provenance, options.material_slot, data.next_id, data, error);
}

} // namespace engine
