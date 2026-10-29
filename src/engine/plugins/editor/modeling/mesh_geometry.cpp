#include "plugins/editor/modeling/mesh_geometry.hpp"

#include "plugins/editor/modeling/mesh_geometry_internal.hpp"

#include <numeric>

namespace engine::plugins::modeling
{
namespace
{

namespace math = detail::geometry;
using math::Point;

bool valid_profile(const std::vector<schema::Vec2>& profile, bool closed, std::string& error)
{
    if (profile.size() < (closed ? 3U : 2U) || profile.size() > 16384)
    {
        error = "Profile must contain a bounded number of points";
        return false;
    }
    for (std::size_t i = 0; i < profile.size(); ++i)
    {
        const auto& p = profile[i];
        if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
        {
            error = "Profile contains a non-finite point";
            return false;
        }
        if (i > 0 || closed)
        {
            const auto& previous = profile[(i + profile.size() - 1) % profile.size()];
            if (std::hypot(p.x() - previous.x(), p.y() - previous.y()) <= math::epsilon)
            {
                error = "Profile contains a zero-length edge";
                return false;
            }
        }
    }
    return true;
}

void reverse_face(schema::ModelingFaceT& face)
{
    std::reverse(face.vertices.begin(), face.vertices.end());
    std::reverse(face.texcoords.begin(), face.texcoords.end());
    std::reverse(face.colors.begin(), face.colors.end());
    for (auto& hole : face.holes)
    {
        std::reverse(hole->vertices.begin(), hole->vertices.end());
        std::reverse(hole->texcoords.begin(), hole->texcoords.end());
        std::reverse(hole->colors.begin(), hole->colors.end());
    }
}

} // namespace

bool MeshGeometry::sweep(const MeshSweepOptions& options, schema::EditableMeshDataT& output,
                         std::string& error)
{
    if (!valid_profile(options.profile, options.closed_profile, error))
    {
        return false;
    }
    if (options.path.size() < 2 || options.path.size() > 16384 ||
        options.profile.size() * options.path.size() > 1000000 ||
        !std::isfinite(options.twist_degrees) || (!options.closed_profile && options.cap))
    {
        error = "Invalid sweep path, twist, or cap for an open profile";
        return false;
    }
    if (options.path.size() > 2 && math::length(math::point(options.path.front()) -
                                                math::point(options.path.back())) <= math::epsilon)
    {
        error = "Sweep currently requires an open path; coincident end caps are not a closed sweep";
        return false;
    }
    std::vector<schema::Vec2> profile = options.profile;
    if (options.closed_profile)
    {
        double area = 0.0;
        for (std::size_t i = 0; i < profile.size(); ++i)
        {
            const auto& a = profile[i];
            const auto& b = profile[(i + 1) % profile.size()];
            area += a.x() * b.y() - a.y() * b.x();
        }
        if (std::abs(area) <= math::epsilon)
        {
            error = "Sweep profile has no area";
            return false;
        }
        if (area < 0.0)
        {
            std::reverse(profile.begin(), profile.end());
        }
    }
    std::vector<Point> tangents(options.path.size());
    std::vector<double> distances(options.path.size(), 0.0);
    for (std::size_t i = 0; i < options.path.size(); ++i)
    {
        if (!math::finite(math::point(options.path[i])))
        {
            error = "Sweep path contains a non-finite point";
            return false;
        }
        if (i + 1 < options.path.size())
        {
            Point segment = math::point(options.path[i + 1]) - math::point(options.path[i]);
            distances[i + 1] = distances[i] + math::length(segment);
            if (!math::normalize(segment))
            {
                error = "Sweep path contains a zero-length segment";
                return false;
            }
            tangents[i] = segment;
        }
        else
        {
            tangents[i] = tangents[i - 1];
        }
    }
    for (std::size_t i = tangents.size() - 1; i > 1; --i)
    {
        Point tangent = tangents[i - 2] + tangents[i - 1];
        if (!math::normalize(tangent))
        {
            error = "Sweep path reverses direction at a joint";
            return false;
        }
        tangents[i - 1] = tangent;
    }
    schema::EditableMeshDataT candidate;
    std::vector<std::vector<std::uint64_t>> rings;
    Point u;
    Point v;
    math::basis(tangents.front(), u, v);
    for (std::size_t i = 0; i < options.path.size(); ++i)
    {
        if (i > 0)
        {
            Point axis = math::cross(tangents[i - 1], tangents[i]);
            const double cosine = std::clamp(math::dot(tangents[i - 1], tangents[i]), -1.0, 1.0);
            if (math::normalize(axis))
            {
                u = math::rotate(u, axis, std::acos(cosine));
            }
            u = u - tangents[i] * math::dot(u, tangents[i]);
            if (!math::normalize(u))
            {
                error = "Cannot transport the sweep coordinate frame";
                return false;
            }
            v = math::cross(tangents[i], u);
        }
        const double fraction = distances[i] / distances.back();
        const double twist = options.twist_degrees * math::pi / 180.0 * fraction;
        const Point tu = u * std::cos(twist) + v * std::sin(twist);
        const Point tv = v * std::cos(twist) - u * std::sin(twist);
        std::vector<std::uint64_t> ring;
        for (const auto& p : profile)
        {
            ring.push_back(math::add_vertex(candidate, math::point(options.path[i]) + tu * p.x() +
                                                           tv * p.y()));
        }
        rings.push_back(std::move(ring));
    }
    const std::size_t edge_count = profile.size() - (options.closed_profile ? 0 : 1);
    for (std::size_t i = 0; i + 1 < rings.size(); ++i)
    {
        for (std::size_t j = 0; j < edge_count; ++j)
        {
            const std::size_t next = (j + 1) % profile.size();
            math::add_quad(candidate, rings[i][j], rings[i][next], rings[i + 1][next],
                           rings[i + 1][j], static_cast<double>(j) / edge_count,
                           static_cast<double>(j + 1) / edge_count, distances[i] / distances.back(),
                           distances[i + 1] / distances.back());
        }
    }
    if (options.cap)
    {
        auto front = rings.front();
        auto front_uv = profile;
        std::reverse(front.begin(), front.end());
        std::reverse(front_uv.begin(), front_uv.end());
        math::add_face(candidate, std::move(front), std::move(front_uv));
        math::add_face(candidate, rings.back(), profile);
    }
    return math::commit(output, std::move(candidate), error);
}

bool MeshGeometry::lathe(const MeshLatheOptions& options, schema::EditableMeshDataT& output,
                         std::string& error)
{
    if (!valid_profile(options.profile, false, error))
    {
        return false;
    }
    if (options.segments < 3 || options.segments > 16384 ||
        options.profile.size() * (options.segments + 1) > 1000000 ||
        !std::isfinite(options.angle_degrees) || options.angle_degrees <= 0.0 ||
        options.angle_degrees > 360.0)
    {
        error = "Invalid lathe segments or angle";
        return false;
    }
    for (const auto& p : options.profile)
    {
        if (p.x() < 0.0)
        {
            error = "Lathe radius must not be negative";
            return false;
        }
    }
    const bool full = std::abs(options.angle_degrees - 360.0) < math::epsilon;
    const std::size_t columns = options.segments + (full ? 0 : 1);
    schema::EditableMeshDataT candidate;
    std::vector<std::vector<std::uint64_t>> rings;
    for (const auto& p : options.profile)
    {
        std::vector<std::uint64_t> ring;
        if (p.x() <= math::epsilon)
        {
            ring.assign(columns, math::add_vertex(candidate, {0.0, p.y(), 0.0}));
        }
        else
        {
            for (std::size_t i = 0; i < columns; ++i)
            {
                const double angle =
                    options.angle_degrees * math::pi / 180.0 * i / options.segments;
                ring.push_back(math::add_vertex(
                    candidate, {p.x() * std::cos(angle), p.y(), -p.x() * std::sin(angle)}));
            }
        }
        rings.push_back(std::move(ring));
    }
    for (std::size_t r = 0; r + 1 < rings.size(); ++r)
    {
        for (std::size_t i = 0; i < options.segments; ++i)
        {
            const auto next = (i + 1) % columns;
            const auto a = rings[r][i];
            const auto b = rings[r][next];
            const auto c = rings[r + 1][next];
            const auto d = rings[r + 1][i];
            const double u0 = static_cast<double>(i) / options.segments;
            const double u1 = static_cast<double>(i + 1) / options.segments;
            const double v0 = static_cast<double>(r) / (rings.size() - 1);
            const double v1 = static_cast<double>(r + 1) / (rings.size() - 1);
            if (a == b && c == d)
            {
                error = "Lathe profile contains an edge entirely on its axis";
                return false;
            }
            if (a == b)
            {
                math::add_face(candidate, {a, c, d}, {{u0, v0}, {u1, v1}, {u0, v1}}, 0, 1);
            }
            else if (c == d)
            {
                math::add_face(candidate, {a, b, c}, {{u0, v0}, {u1, v0}, {u1, v1}}, 0, 1);
            }
            else
            {
                math::add_quad(candidate, a, b, c, d, u0, u1, v0, v1);
            }
        }
    }
    if (options.cap)
    {
        std::uint64_t bottom = rings.front().front();
        std::uint64_t top = rings.back().front();
        if (options.profile.front().x() > math::epsilon)
        {
            bottom = math::add_vertex(candidate, {0.0, options.profile.front().y(), 0.0});
            for (std::size_t i = 0; i < options.segments; ++i)
            {
                math::add_face(candidate,
                               {bottom, rings.front()[(i + 1) % columns], rings.front()[i]});
            }
        }
        if (options.profile.back().x() > math::epsilon)
        {
            top = math::add_vertex(candidate, {0.0, options.profile.back().y(), 0.0});
            for (std::size_t i = 0; i < options.segments; ++i)
            {
                math::add_face(candidate, {top, rings.back()[i], rings.back()[(i + 1) % columns]});
            }
        }
        if (!full)
        {
            std::vector<std::uint64_t> start;
            std::vector<std::uint64_t> end;
            for (const auto& ring : rings)
            {
                start.push_back(ring.front());
                end.push_back(ring.back());
            }
            if (start.back() != top)
            {
                start.push_back(top);
                end.push_back(top);
            }
            if (start.front() != bottom)
            {
                start.push_back(bottom);
                end.push_back(bottom);
            }
            std::reverse(end.begin(), end.end());
            math::add_face(candidate, std::move(start));
            math::add_face(candidate, std::move(end));
        }
    }
    return math::commit(output, std::move(candidate), error);
}

bool MeshGeometry::mirror(schema::EditableMeshDataT& data, const MeshMirrorOptions& options,
                          std::string& error)
{
    error.clear();
    Point normal = math::point(options.normal);
    const double normal_length = math::length(normal);
    if (!math::normalize(normal) || !std::isfinite(options.offset) ||
        !std::isfinite(options.tolerance) || options.tolerance <= 0.0 ||
        !EditableMesh::validate(data, error))
    {
        if (error.empty())
        {
            error = "Invalid mirror plane or tolerance";
        }
        return false;
    }
    const double offset = options.offset / normal_length;
    if (options.require_half_space)
    {
        bool positive = false;
        bool negative = false;
        for (const auto& vertex : data.vertices)
        {
            const double distance = math::dot(normal, math::point(vertex)) - offset;
            positive = positive || distance > options.tolerance;
            negative = negative || distance < -options.tolerance;
        }
        if (positive && negative)
        {
            error = "Continuous mirror source must stay in one half-space of its plane";
            return false;
        }
    }
    schema::EditableMeshDataT candidate = data;
    if (!options.duplicate)
    {
        for (auto& vertex : candidate.vertices)
        {
            const Point p = math::point(vertex);
            const Point reflected = p - normal * (2.0 * (math::dot(normal, p) - offset));
            vertex = schema::ModelingVertex(vertex.id(), reflected.x, reflected.y, reflected.z);
        }
        for (auto& face : candidate.faces)
        {
            reverse_face(*face);
        }
        return math::commit(data, std::move(candidate), error);
    }
    std::map<std::uint64_t, std::uint64_t> remap;
    bool has_off_plane_vertex = false;
    for (std::size_t i = 0; i < data.vertices.size(); ++i)
    {
        const auto& vertex = data.vertices[i];
        const Point p = math::point(vertex);
        const double distance = math::dot(normal, p) - offset;
        if (options.weld && std::abs(distance) <= options.tolerance)
        {
            remap[vertex.id()] = vertex.id();
            const Point projected = p - normal * distance;
            candidate.vertices[i] =
                schema::ModelingVertex(vertex.id(), projected.x, projected.y, projected.z);
        }
        else
        {
            has_off_plane_vertex = true;
            remap[vertex.id()] = math::add_vertex(candidate, p - normal * (2.0 * distance));
        }
    }
    const auto on_plane = [&](const schema::ModelingFaceT& face)
    {
        return std::all_of(face.vertices.begin(), face.vertices.end(),
                           [&](const auto id) { return remap[id] == id; });
    };
    if (options.weld && has_off_plane_vertex)
    {
        std::erase_if(candidate.faces, [&](const auto& face) { return on_plane(*face); });
    }
    for (const auto& source : data.faces)
    {
        if (options.weld && on_plane(*source))
        {
            continue;
        }
        auto face = std::make_unique<schema::ModelingFaceT>(*source);
        face->id = candidate.next_id++;
        for (auto& id : face->vertices)
        {
            id = remap[id];
        }
        for (auto& hole : face->holes)
        {
            for (auto& id : hole->vertices)
            {
                id = remap[id];
            }
        }
        reverse_face(*face);
        candidate.faces.push_back(std::move(face));
    }
    math::remove_unused_vertices(candidate);
    return math::commit(data, std::move(candidate), error);
}

} // namespace engine::plugins::modeling
