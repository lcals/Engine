#pragma once

#include "plugins/editor/modeling/mesh_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <set>
#include <utility>

namespace engine::plugins::modeling::detail::geometry
{

constexpr double epsilon = 1.0e-9;
constexpr double pi = std::numbers::pi_v<double>;

struct Point
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline Point point(const schema::Vec3& value)
{
    return {value.x(), value.y(), value.z()};
}

inline Point point(const schema::ModelingVertex& value)
{
    return {value.x(), value.y(), value.z()};
}

inline Point operator+(Point a, Point b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Point operator-(Point a, Point b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Point operator*(Point a, double scale)
{
    return {a.x * scale, a.y * scale, a.z * scale};
}

inline double dot(Point a, Point b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Point cross(Point a, Point b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline double length(Point value)
{
    return std::sqrt(dot(value, value));
}

inline bool finite(Point value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

inline bool normalize(Point& value)
{
    const double size = length(value);
    if (!finite(value) || !std::isfinite(size) || size <= epsilon)
    {
        return false;
    }
    value = value * (1.0 / size);
    return true;
}

inline Point rotate(Point value, Point axis, double angle)
{
    const double cosine = std::cos(angle);
    return value * cosine + cross(axis, value) * std::sin(angle) +
           axis * (dot(axis, value) * (1.0 - cosine));
}

inline void basis(Point normal, Point& u, Point& v)
{
    const Point up = std::abs(normal.y) < 0.9 ? Point{0.0, 1.0, 0.0} : Point{0.0, 0.0, 1.0};
    u = cross(up, normal);
    const bool valid = normalize(u);
    (void)valid;
    v = cross(normal, u);
}

inline Point position(const schema::EditableMeshDataT& data, std::uint64_t id)
{
    const auto* vertex = EditableMesh::find_vertex(data, id);
    return vertex == nullptr ? Point{} : point(*vertex);
}

inline bool face_normal(const schema::EditableMeshDataT& data, const schema::ModelingFaceT& face,
                        Point& normal)
{
    normal = {};
    for (std::size_t i = 0; i < face.vertices.size(); ++i)
    {
        const Point a = position(data, face.vertices[i]);
        const Point b = position(data, face.vertices[(i + 1) % face.vertices.size()]);
        normal = normal + cross(a, b);
    }
    return normalize(normal);
}

inline std::uint64_t add_vertex(schema::EditableMeshDataT& data, Point value)
{
    const std::uint64_t id = data.next_id++;
    data.vertices.emplace_back(id, value.x, value.y, value.z);
    return id;
}

inline void add_face(schema::EditableMeshDataT& data, std::vector<std::uint64_t> vertices,
                     std::vector<schema::Vec2> texcoords = {}, std::uint32_t material = 0,
                     std::uint32_t smoothing = 0)
{
    auto face = std::make_unique<schema::ModelingFaceT>();
    face->id = data.next_id++;
    face->vertices = std::move(vertices);
    face->texcoords = std::move(texcoords);
    face->material_slot = material;
    face->smoothing_group = smoothing;
    data.faces.push_back(std::move(face));
}

inline void add_quad(schema::EditableMeshDataT& data, std::uint64_t a, std::uint64_t b,
                     std::uint64_t c, std::uint64_t d, double u0, double u1, double v0, double v1)
{
    add_face(data, {a, b, c}, {{u0, v0}, {u1, v0}, {u1, v1}}, 0, 1);
    add_face(data, {a, c, d}, {{u0, v0}, {u1, v1}, {u0, v1}}, 0, 1);
}

inline bool commit(schema::EditableMeshDataT& output, schema::EditableMeshDataT&& candidate,
                   std::string& error)
{
    if (!EditableMesh::validate(candidate, error))
    {
        return false;
    }
    output = std::move(candidate);
    error.clear();
    return true;
}

inline bool selected_faces(schema::EditableMeshDataT& data,
                           std::span<const std::uint64_t> selection,
                           std::vector<schema::ModelingFaceT*>& output, std::string& error)
{
    output.clear();
    if (!EditableMesh::validate(data, error))
    {
        return false;
    }
    if (selection.empty())
    {
        for (auto& face : data.faces)
        {
            output.push_back(face.get());
        }
    }
    else
    {
        std::set<std::uint64_t> ids;
        for (const auto id : selection)
        {
            auto* face = EditableMesh::find_face(data, id);
            if (face == nullptr || !ids.insert(id).second)
            {
                error = "Face selection contains a missing or duplicate identity";
                output.clear();
                return false;
            }
            output.push_back(face);
        }
    }
    if (output.empty())
    {
        error = "The operation requires at least one face";
        return false;
    }
    return true;
}

inline void remove_unused_vertices(schema::EditableMeshDataT& data)
{
    std::set<std::uint64_t> used;
    for (const auto& face : data.faces)
    {
        used.insert(face->vertices.begin(), face->vertices.end());
        for (const auto& hole : face->holes)
        {
            used.insert(hole->vertices.begin(), hole->vertices.end());
        }
    }
    std::erase_if(data.vertices,
                  [&used](const auto& vertex) { return !used.contains(vertex.id()); });
}

inline std::pair<std::uint64_t, std::uint64_t> edge_key(std::uint64_t a, std::uint64_t b)
{
    return std::minmax(a, b);
}

} // namespace engine::plugins::modeling::detail::geometry
