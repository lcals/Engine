#pragma once

#include "plugins/editor/modeling/editable_mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace engine::modeling_detail
{

using Vector = std::array<double, 3>;
using Point = std::array<double, 2>;
constexpr double epsilon = 1.0e-9;

inline Vector add(const Vector& a, const Vector& b)
{
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

inline Vector sub(const Vector& a, const Vector& b)
{
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

inline Vector mul(const Vector& a, double value)
{
    return {a[0] * value, a[1] * value, a[2] * value};
}

inline double dot(const Vector& a, const Vector& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline Vector cross(const Vector& a, const Vector& b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

inline double length(const Vector& value)
{
    return std::hypot(value[0], value[1], value[2]);
}

inline Vector normalized(const Vector& value)
{
    const auto size = length(value);
    return size > epsilon ? mul(value, 1.0 / size) : Vector{};
}

inline bool finite(const Vector& value)
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

inline Vector position(const schema::ModelingVertex& vertex)
{
    return {vertex.x(), vertex.y(), vertex.z()};
}

inline Vector position(const schema::EditableMeshDataT& data, std::uint64_t id)
{
    const auto* value = EditableMesh::find_vertex(data, id);
    return value ? position(*value) : Vector{};
}

inline Vector face_normal(const schema::EditableMeshDataT& data, const schema::ModelingFaceT& face)
{
    Vector normal{};
    for (std::size_t i = 0; i < face.vertices.size(); ++i)
    {
        const auto a = position(data, face.vertices[i]);
        const auto b = position(data, face.vertices[(i + 1) % face.vertices.size()]);
        normal[0] += (a[1] - b[1]) * (a[2] + b[2]);
        normal[1] += (a[2] - b[2]) * (a[0] + b[0]);
        normal[2] += (a[0] - b[0]) * (a[1] + b[1]);
    }
    return normal;
}

inline int projection_axis(const Vector& normal)
{
    const auto x = std::abs(normal[0]);
    const auto y = std::abs(normal[1]);
    const auto z = std::abs(normal[2]);
    return x > y && x > z ? 0 : (y > z ? 1 : 2);
}

inline Point project(const Vector& value, int axis)
{
    return axis == 0 ? Point{value[1], value[2]}
                     : (axis == 1 ? Point{value[2], value[0]} : Point{value[0], value[1]});
}

inline std::uint64_t add_vertex(schema::EditableMeshDataT& data, const Vector& value)
{
    const auto id = data.next_id++;
    data.vertices.emplace_back(id, value[0], value[1], value[2]);
    return id;
}

inline schema::ModelingFaceT& add_face(schema::EditableMeshDataT& data,
                                       std::vector<std::uint64_t> vertices,
                                       std::uint32_t material = 0, std::uint32_t smoothing = 0)
{
    auto face = std::make_unique<schema::ModelingFaceT>();
    face->id = data.next_id++;
    face->vertices = std::move(vertices);
    face->material_slot = material;
    face->smoothing_group = smoothing;
    data.faces.push_back(std::move(face));
    return *data.faces.back();
}

inline void set_position(schema::EditableMeshDataT& data, std::uint64_t id, const Vector& value)
{
    for (auto& vertex : data.vertices)
    {
        if (vertex.id() == id)
        {
            vertex = schema::ModelingVertex(id, value[0], value[1], value[2]);
            return;
        }
    }
}

inline schema::Vec2 corner_uv(const schema::ModelingFaceT& face, std::size_t index)
{
    return face.texcoords.empty() ? schema::Vec2(0.0, 0.0) : face.texcoords[index];
}

inline schema::Color corner_color(const schema::ModelingFaceT& face, std::size_t index)
{
    return face.colors.empty() ? schema::Color(1.0F, 1.0F, 1.0F, 1.0F) : face.colors[index];
}

inline void reverse_face(schema::ModelingFaceT& face)
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

} // namespace engine::modeling_detail
