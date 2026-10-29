#include "plugins/editor/modeling/mesh_geometry.hpp"

#include "plugins/editor/modeling/mesh_primitives.hpp"
#include "plugins/editor/modeling/mesh_topology.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>
#include <set>
#include <vector>

namespace
{

bool expect(bool condition, const char* message, const std::string& error = {})
{
    if (!condition)
    {
        std::fprintf(stderr, "modeling geometry: %s: %s\n", message, error.c_str());
    }
    return condition;
}

bool near(double a, double b, double tolerance = 0.000001)
{
    return std::abs(a - b) <= tolerance;
}

double volume(const engine::schema::EditableMeshDataT& data)
{
    double result = 0.0;
    for (const auto& face : data.faces)
    {
        std::vector<std::uint32_t> indices;
        std::string error;
        if (!engine::plugins::modeling::EditableMesh::triangulate_face(data, *face, indices, error))
        {
            return -1.0;
        }
        auto corners = face->vertices;
        for (const auto& hole : face->holes)
        {
            corners.insert(corners.end(), hole->vertices.begin(), hole->vertices.end());
        }
        for (std::size_t i = 0; i < indices.size(); i += 3)
        {
            const auto* a =
                engine::plugins::modeling::EditableMesh::find_vertex(data, corners[indices[i]]);
            const auto* b =
                engine::plugins::modeling::EditableMesh::find_vertex(data, corners[indices[i + 1]]);
            const auto* c =
                engine::plugins::modeling::EditableMesh::find_vertex(data, corners[indices[i + 2]]);
            result += (a->x() * (b->y() * c->z() - b->z() * c->y()) +
                       a->y() * (b->z() * c->x() - b->x() * c->z()) +
                       a->z() * (b->x() * c->y() - b->y() * c->x())) /
                      6.0;
        }
    }
    return result;
}

bool closed(const engine::schema::EditableMeshDataT& data)
{
    std::map<std::pair<std::uint64_t, std::uint64_t>, int> edges;
    for (const auto& face : data.faces)
    {
        const auto add = [&](const auto& loop)
        {
            for (std::size_t i = 0; i < loop.size(); ++i)
            {
                ++edges[{loop[i], loop[(i + 1) % loop.size()]}];
            }
        };
        add(face->vertices);
        for (const auto& hole : face->holes)
        {
            add(hole->vertices);
        }
    }
    for (const auto& [edge, count] : edges)
    {
        const auto reverse = edges.find({edge.second, edge.first});
        if (count != 1 || reverse == edges.end() || reverse->second != 1)
        {
            return false;
        }
    }
    return !edges.empty();
}

bool same(const engine::schema::EditableMeshDataT& a, const engine::schema::EditableMeshDataT& b)
{
    std::vector<std::uint8_t> first;
    std::vector<std::uint8_t> second;
    std::string error;
    return engine::plugins::modeling::EditableMesh::serialize(a, first, error) &&
           engine::plugins::modeling::EditableMesh::serialize(b, second, error) && first == second;
}

bool cube(engine::schema::EditableMeshDataT& data, std::string& error)
{
    return engine::plugins::modeling::MeshPrimitives::create({}, data, error);
}

bool translate(engine::schema::EditableMeshDataT& data, double x, double y, double z,
               std::string& error)
{
    const std::array<double, 16> matrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1};
    engine::plugins::modeling::MeshSelection selection;
    selection.all = true;
    return engine::plugins::modeling::MeshTopology::transform(data, selection, matrix, error);
}

bool booleans_and_clipping()
{
    std::string error;
    engine::schema::EditableMeshDataT first;
    engine::schema::EditableMeshDataT second;
    if (!expect(cube(first, error) && cube(second, error), "create boolean inputs", error) ||
        !expect(closed(first) && near(volume(first), 1.0), "cube topology and volume") ||
        !expect(translate(second, 0.5, 0, 0, error), "offset second cube", error))
    {
        return false;
    }
    for (auto& face : first.faces)
    {
        face->material_slot = 2;
    }
    for (auto& face : second.faces)
    {
        face->material_slot = 3;
    }
    const std::array operations = {engine::plugins::modeling::MeshBooleanOperation::unite,
                                   engine::plugins::modeling::MeshBooleanOperation::subtract,
                                   engine::plugins::modeling::MeshBooleanOperation::intersect};
    const std::array expected = {1.5, 0.5, 0.5};
    for (std::size_t i = 0; i < operations.size(); ++i)
    {
        auto result = first;
        if (!expect(engine::plugins::modeling::MeshGeometry::boolean_operation(
                        result, second, operations[i], error),
                    "perform solid boolean", error) ||
            !expect(closed(result) && near(volume(result), expected[i]),
                    "boolean produces closed solid with exact expected volume"))
        {
            return false;
        }
        for (const auto& face : result.faces)
        {
            if (!expect((face->material_slot == 2 || face->material_slot == 3) &&
                            face->texcoords.size() == face->vertices.size(),
                        "boolean retains face material provenance and corner UV"))
            {
                return false;
            }
        }
    }
    auto clipped = first;
    engine::plugins::modeling::MeshClipOptions clip;
    clip.cap_material_slot = 7;
    if (!expect(engine::plugins::modeling::MeshGeometry::clip(clipped, clip, error),
                "cap half-space cut", error) ||
        !expect(closed(clipped) && near(volume(clipped), 0.5), "cut is closed half cube"))
    {
        return false;
    }
    bool cap_found = false;
    for (const auto& face : clipped.faces)
    {
        cap_found = cap_found || face->material_slot == 7;
    }
    if (!expect(cap_found, "cut assigns requested cap material"))
    {
        return false;
    }
    auto open = first;
    clip.cap = false;
    if (!expect(engine::plugins::modeling::MeshGeometry::clip(open, clip, error), "uncapped cut",
                error) ||
        !expect(!closed(open), "uncapped cut leaves boundary"))
    {
        return false;
    }
    const auto original = open;
    if (!expect(!engine::plugins::modeling::MeshGeometry::boolean_operation(
                    open, second, engine::plugins::modeling::MeshBooleanOperation::unite, error),
                "reject nonmanifold boolean input") ||
        !expect(same(original, open), "failed boolean leaves model unchanged"))
    {
        return false;
    }
    auto empty = first;
    if (!expect(engine::plugins::modeling::MeshGeometry::boolean_operation(
                    empty, first, engine::plugins::modeling::MeshBooleanOperation::subtract, error),
                "subtract identical solids", error) ||
        !expect(empty.faces.empty() && empty.vertices.empty(), "empty difference is valid"))
    {
        return false;
    }
    engine::plugins::modeling::MeshMirrorOptions mirror;
    mirror.require_half_space = true;
    auto straddling = first;
    if (!expect(!engine::plugins::modeling::MeshGeometry::mirror(straddling, mirror, error),
                "continuous mirror rejects a source crossing its plane") ||
        !expect(same(straddling, first), "invalid mirror source remains unchanged"))
    {
        return false;
    }
    return expect(engine::plugins::modeling::MeshGeometry::mirror(clipped, mirror, error),
                  "mirror half cube", error) &&
           expect(closed(clipped) && near(volume(clipped), 1.0),
                  "mirror weld removes internal cap and restores complete cube");
}

bool edge_bevel()
{
    std::string error;
    engine::schema::EditableMeshDataT source;
    if (!expect(cube(source, error), "bevel input", error))
    {
        return false;
    }
    engine::plugins::modeling::MeshBevelOptions options;
    options.edges.push_back({source.faces.front()->vertices[0], source.faces.front()->vertices[1]});
    options.width = 0.1;
    auto chamfer = source;
    if (!expect(engine::plugins::modeling::MeshGeometry::bevel(chamfer, options, error),
                "single-edge bevel", error) ||
        !expect(closed(chamfer) && near(volume(chamfer), 0.995),
                "one segment removes the expected edge wedge"))
    {
        return false;
    }
    options.segments = 4;
    auto round = source;
    if (!expect(engine::plugins::modeling::MeshGeometry::bevel(round, options, error),
                "multi-segment bevel", error) ||
        !expect(closed(round) && volume(round) > volume(chamfer) && volume(round) < 1.0 &&
                    round.faces.size() > chamfer.faces.size(),
                "round bevel adds real surface segments and changes volume"))
    {
        return false;
    }
    options.profile = 0.8;
    auto profile = source;
    if (!expect(engine::plugins::modeling::MeshGeometry::bevel(profile, options, error),
                "profile bevel", error) ||
        !expect(closed(profile) && volume(profile) > volume(round),
                "profile changes geometric section"))
    {
        return false;
    }
    options.width = 2.0;
    auto invalid = source;
    if (!expect(!engine::plugins::modeling::MeshGeometry::bevel(invalid, options, error),
                "reject oversized bevel") ||
        !expect(same(invalid, source), "invalid bevel leaves topology unchanged"))
    {
        return false;
    }
    options.width = 0.1;
    options.profile = 0.5;
    options.edges.clear();
    std::set<std::pair<std::uint64_t, std::uint64_t>> unique;
    for (const auto& face : source.faces)
    {
        for (std::size_t i = 0; i < face->vertices.size(); ++i)
        {
            const auto a = face->vertices[i];
            const auto b = face->vertices[(i + 1) % face->vertices.size()];
            const auto key = std::minmax(a, b);
            if (unique.emplace(key.first, key.second).second)
            {
                options.edges.push_back({key.first, key.second});
            }
        }
    }
    auto all = source;
    return expect(engine::plugins::modeling::MeshGeometry::bevel(all, options, error),
                  "all cube edges bevel", error) &&
           expect(closed(all) && volume(all) > 0.9 && volume(all) < 1.0,
                  "multiple edge joints remain a closed solid");
}

bool sweeps_and_lathe()
{
    std::string error;
    engine::plugins::modeling::MeshSweepOptions sweep;
    sweep.profile = {{-0.5, -0.5}, {0.5, -0.5}, {0.5, 0.5}, {-0.5, 0.5}};
    sweep.path = {{0, 0, 0}, {0, 0, 2}};
    engine::schema::EditableMeshDataT mesh;
    if (!expect(engine::plugins::modeling::MeshGeometry::sweep(sweep, mesh, error),
                "square profile sweep", error) ||
        !expect(closed(mesh) && near(volume(mesh), 2.0),
                "sweep is closed prism of expected volume"))
    {
        return false;
    }
    sweep.path = {{0, 0, 0}, {0, 0, 2}, {1, 0, 3}};
    sweep.twist_degrees = 45.0;
    if (!expect(engine::plugins::modeling::MeshGeometry::sweep(sweep, mesh, error),
                "bent and twisted sweep", error) ||
        !expect(closed(mesh) && volume(mesh) > 0.0, "bent sweep preserves oriented closure"))
    {
        return false;
    }
    const auto previous = mesh;
    sweep.path = {{0, 0, 0}, {0, 0, 0}};
    if (!expect(!engine::plugins::modeling::MeshGeometry::sweep(sweep, mesh, error),
                "reject degenerate sweep") ||
        !expect(same(mesh, previous), "failed generator preserves output"))
    {
        return false;
    }
    sweep.path = {{0, 0, 0}, {0, 0, 2}, {2, 0, 2}, {0, 0, 0}};
    if (!expect(!engine::plugins::modeling::MeshGeometry::sweep(sweep, mesh, error),
                "reject closed sweep until the frame seam is supported") ||
        !expect(same(mesh, previous), "unsupported closed sweep preserves output"))
    {
        return false;
    }
    engine::plugins::modeling::MeshLatheOptions lathe;
    lathe.profile = {{1, 0}, {1, 1}};
    lathe.segments = 32;
    const double expected = 16.0 * std::sin(2.0 * std::numbers::pi_v<double> / 32.0);
    if (!expect(engine::plugins::modeling::MeshGeometry::lathe(lathe, mesh, error),
                "lathe cylinder", error) ||
        !expect(closed(mesh) && near(volume(mesh), expected), "lathe closed cylinder volume"))
    {
        return false;
    }
    lathe.angle_degrees = 180;
    const double half_expected = 16.0 * std::sin(std::numbers::pi_v<double> / 32.0);
    if (!expect(engine::plugins::modeling::MeshGeometry::lathe(lathe, mesh, error), "partial lathe",
                error) ||
        !expect(closed(mesh) && near(volume(mesh), half_expected), "partial lathe seals both ends"))
    {
        return false;
    }
    lathe.angle_degrees = 360;
    lathe.profile = {{0, 0}, {1, 1}};
    return expect(engine::plugins::modeling::MeshGeometry::lathe(lathe, mesh, error),
                  "lathe axis tip", error) &&
           expect(closed(mesh) && near(volume(mesh), expected / 3.0),
                  "axis tip is one shared vertex without degenerate triangles");
}

bool uv_and_smoothing()
{
    std::string error;
    engine::schema::EditableMeshDataT mesh;
    if (!expect(cube(mesh, error), "UV input", error))
    {
        return false;
    }
    engine::plugins::modeling::MeshUvProjectionOptions project;
    project.projection = engine::plugins::modeling::MeshUvProjection::box;
    if (!expect(engine::plugins::modeling::MeshGeometry::project_uv(mesh, {}, project, error),
                "box UV", error) ||
        !expect(engine::plugins::modeling::MeshGeometry::unwrap_uv(mesh, {}, {}, error),
                "UV islands", error))
    {
        return false;
    }
    for (const auto& face : mesh.faces)
    {
        for (const auto& uv : face->texcoords)
        {
            if (!expect(uv.x() >= 0.0 && uv.x() <= 1.0 && uv.y() >= 0.0 && uv.y() <= 1.0,
                        "UV island fits atlas"))
            {
                return false;
            }
        }
    }
    engine::plugins::modeling::MeshHotspotOptions hotspot;
    hotspot.rectangles = {{0.5, 0.5, 0.25, 0.25}, {0.0, 0.0, 0.4, 0.1}};
    hotspot.padding = 0.01;
    if (!expect(engine::plugins::modeling::MeshGeometry::hotspot_uv(mesh, {}, hotspot, error),
                "Hotspot mapping", error))
    {
        return false;
    }
    for (const auto& face : mesh.faces)
    {
        for (const auto& uv : face->texcoords)
        {
            if (!expect(uv.x() >= 0.51 - 0.000001 && uv.x() <= 0.74 + 0.000001 &&
                            uv.y() >= 0.51 - 0.000001 && uv.y() <= 0.74 + 0.000001,
                        "square face selects square Hotspot with padding"))
            {
                return false;
            }
        }
    }
    engine::plugins::modeling::MeshUvTransformOptions transform;
    transform.offset = {1, 2};
    const auto before = mesh.faces.front()->texcoords.front();
    if (!expect(engine::plugins::modeling::MeshGeometry::transform_uv(mesh, {}, transform, error),
                "UV transform", error) ||
        !expect(near(mesh.faces.front()->texcoords.front().x(), before.x() + 1) &&
                    near(mesh.faces.front()->texcoords.front().y(), before.y() + 2),
                "UV transform changes coordinates") ||
        !expect(engine::plugins::modeling::MeshGeometry::auto_smooth(mesh, 45, error),
                "auto smoothing", error))
    {
        return false;
    }
    std::set<std::uint32_t> groups;
    for (const auto& face : mesh.faces)
    {
        groups.insert(face->smoothing_group);
    }
    if (!expect(groups.size() == 6, "cube right-angle faces remain separate smoothing groups") ||
        !expect(engine::plugins::modeling::MeshGeometry::set_smoothing(mesh, {}, 2, error),
                "explicit smoothing", error))
    {
        return false;
    }
    const auto previous = mesh;
    hotspot.rectangles = {{0, 0, -1, 1}};
    return expect(!engine::plugins::modeling::MeshGeometry::hotspot_uv(mesh, {}, hotspot, error),
                  "reject invalid atlas") &&
           expect(same(mesh, previous), "failed UV edit preserves model");
}

using UvTriangle = std::array<engine::schema::Vec2, 3>;

double uv_cross(const engine::schema::Vec2& a, const engine::schema::Vec2& b,
                const engine::schema::Vec2& c)
{
    return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
}

double overlap_area(const UvTriangle& first, const UvTriangle& second)
{
    std::vector<engine::schema::Vec2> polygon(first.begin(), first.end());
    const double direction = uv_cross(second[0], second[1], second[2]) >= 0.0 ? 1.0 : -1.0;
    for (std::size_t i = 0; i < 3 && !polygon.empty(); ++i)
    {
        const auto& a = second[i];
        const auto& b = second[(i + 1) % 3];
        std::vector<engine::schema::Vec2> clipped;
        for (std::size_t j = 0; j < polygon.size(); ++j)
        {
            const auto& p = polygon[j];
            const auto& q = polygon[(j + 1) % polygon.size()];
            const double dp = uv_cross(a, b, p) * direction;
            const double dq = uv_cross(a, b, q) * direction;
            if (dp >= 0.0)
            {
                clipped.push_back(p);
            }
            if ((dp >= 0.0) != (dq >= 0.0))
            {
                const double t = dp / (dp - dq);
                clipped.emplace_back(p.x() + (q.x() - p.x()) * t, p.y() + (q.y() - p.y()) * t);
            }
        }
        polygon = std::move(clipped);
    }
    double area = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i)
    {
        const auto& a = polygon[i];
        const auto& b = polygon[(i + 1) % polygon.size()];
        area += a.x() * b.y() - a.y() * b.x();
    }
    return std::abs(area) * 0.5;
}

bool no_uv_overlap(const engine::schema::EditableMeshDataT& mesh, std::string& error)
{
    std::vector<UvTriangle> triangles;
    for (const auto& face : mesh.faces)
    {
        std::vector<std::uint32_t> indices;
        if (!engine::plugins::modeling::EditableMesh::triangulate_face(mesh, *face, indices, error))
        {
            return false;
        }
        auto uv = face->texcoords;
        for (const auto& hole : face->holes)
        {
            uv.insert(uv.end(), hole->texcoords.begin(), hole->texcoords.end());
        }
        for (std::size_t i = 0; i < indices.size(); i += 3)
        {
            triangles.push_back({uv[indices[i]], uv[indices[i + 1]], uv[indices[i + 2]]});
        }
    }
    for (std::size_t i = 0; i < triangles.size(); ++i)
    {
        if (std::abs(uv_cross(triangles[i][0], triangles[i][1], triangles[i][2])) < 1e-10)
        {
            error = "Atlas contains a zero-area UV triangle";
            return false;
        }
        for (std::size_t j = i + 1; j < triangles.size(); ++j)
        {
            if (overlap_area(triangles[i], triangles[j]) > 1e-8)
            {
                error = "Atlas contains overlapping UV triangles";
                return false;
            }
        }
    }
    return true;
}

bool edge_uv_continuous(const engine::schema::EditableMeshDataT& mesh,
                        engine::plugins::modeling::MeshEdge edge)
{
    std::vector<std::pair<engine::schema::Vec2, engine::schema::Vec2>> sides;
    for (const auto& face : mesh.faces)
    {
        for (std::size_t i = 0; i < face->vertices.size(); ++i)
        {
            const auto next = (i + 1) % face->vertices.size();
            if (face->vertices[i] == edge.first && face->vertices[next] == edge.second)
            {
                sides.emplace_back(face->texcoords[i], face->texcoords[next]);
            }
            else if (face->vertices[i] == edge.second && face->vertices[next] == edge.first)
            {
                sides.emplace_back(face->texcoords[next], face->texcoords[i]);
            }
        }
    }
    return sides.size() == 2 && near(sides[0].first.x(), sides[1].first.x()) &&
           near(sides[0].first.y(), sides[1].first.y()) &&
           near(sides[0].second.x(), sides[1].second.x()) &&
           near(sides[0].second.y(), sides[1].second.y());
}

bool atlas_unwrap()
{
    std::string error;
    engine::schema::EditableMeshDataT mesh;
    engine::plugins::modeling::MeshUvUnwrapOptions options;
    options.resolution = 256;
    options.padding = 0.01;
    if (!expect(cube(mesh, error), "atlas cube input", error) ||
        !expect(engine::plugins::modeling::MeshGeometry::unwrap_uv(mesh, {}, options, error),
                "atlas cube", error) ||
        !expect(mesh.faces.size() == 6 && closed(mesh) && near(volume(mesh), 1.0),
                "atlas preserves cube polygons and geometry") ||
        !expect(no_uv_overlap(mesh, error), "cube atlas has no overlapping triangles", error))
    {
        return false;
    }
    engine::plugins::modeling::MeshPrimitiveOptions primitive;
    primitive.type = engine::plugins::modeling::MeshPrimitive::cylinder;
    primitive.segments = 16;
    if (!expect(engine::plugins::modeling::MeshPrimitives::create(primitive, mesh, error),
                "atlas curved input", error))
    {
        return false;
    }
    const auto original = mesh;
    if (!expect(engine::plugins::modeling::MeshGeometry::unwrap_uv(mesh, {}, options, error),
                "atlas curved mesh", error) ||
        !expect(no_uv_overlap(mesh, error), "curved atlas has no overlapping triangles", error))
    {
        return false;
    }
    std::size_t continuous = 0;
    std::vector<engine::plugins::modeling::MeshEdge> vertical;
    std::set<std::pair<std::uint64_t, std::uint64_t>> visited;
    for (const auto& face : mesh.faces)
    {
        for (std::size_t i = 0; i < face->vertices.size(); ++i)
        {
            const auto a = face->vertices[i];
            const auto b = face->vertices[(i + 1) % face->vertices.size()];
            const auto key = std::minmax(a, b);
            if (!visited.emplace(key.first, key.second).second)
            {
                continue;
            }
            const auto* va = engine::plugins::modeling::EditableMesh::find_vertex(mesh, a);
            const auto* vb = engine::plugins::modeling::EditableMesh::find_vertex(mesh, b);
            if (near(va->x(), vb->x()) && near(va->z(), vb->z()) && !near(va->y(), vb->y()))
            {
                vertical.push_back({a, b});
                continuous += edge_uv_continuous(mesh, {a, b}) ? 1 : 0;
            }
        }
    }
    if (!expect(continuous > 0 && !vertical.empty(),
                "curved chart spans adjacent source polygons instead of one island per face"))
    {
        return false;
    }
    options.seams.push_back(vertical.front());
    mesh = original;
    if (!expect(engine::plugins::modeling::MeshGeometry::unwrap_uv(mesh, {}, options, error),
                "explicit atlas seam", error) ||
        !expect(!edge_uv_continuous(mesh, vertical.front()), "atlas preserves requested seam") ||
        !expect(no_uv_overlap(mesh, error), "seam atlas remains nonoverlapping", error))
    {
        return false;
    }
    const auto before = mesh;
    options.seams = {{9999999, 9999998}};
    return expect(!engine::plugins::modeling::MeshGeometry::unwrap_uv(mesh, {}, options, error),
                  "reject absent UV seam") &&
           expect(same(mesh, before), "invalid seam leaves UV and topology unchanged");
}

} // namespace

int main()
{
    return booleans_and_clipping() && edge_bevel() && sweeps_and_lathe() && uv_and_smoothing() &&
                   atlas_unwrap()
               ? 0
               : 1;
}
