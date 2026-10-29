#include "plugins/modeling/mesh_primitives.hpp"

#include "plugins/modeling/modeling_math.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <tuple>
#include <utility>

namespace engine
{
namespace
{

using namespace modeling_detail;
using Id = std::uint64_t;
constexpr double pi = std::numbers::pi_v<double>;

bool fail(std::string& error, std::string message)
{
    error = std::move(message);
    return false;
}

void box(schema::EditableMeshDataT& data, const Vector& size)
{
    std::vector<Id> vertices;
    for (const auto& point : std::vector<Vector>{{-1, -1, -1},
                                                 {1, -1, -1},
                                                 {1, 1, -1},
                                                 {-1, 1, -1},
                                                 {-1, -1, 1},
                                                 {1, -1, 1},
                                                 {1, 1, 1},
                                                 {-1, 1, 1}})
    {
        vertices.push_back(add_vertex(
            data, {point[0] * size[0] * 0.5, point[1] * size[1] * 0.5, point[2] * size[2] * 0.5}));
    }
    for (const auto& indices : std::vector<std::array<std::size_t, 4>>{
             {0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3}, {1, 2, 6, 5}, {3, 7, 6, 2}, {0, 1, 5, 4}})
    {
        auto& face = add_face(data, {vertices[indices[0]], vertices[indices[1]],
                                     vertices[indices[2]], vertices[indices[3]]});
        face.texcoords = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    }
}

void extruded_outline(schema::EditableMeshDataT& data, const std::vector<Point>& points,
                      double depth)
{
    std::vector<Id> back;
    std::vector<Id> front;
    for (const auto& point : points)
    {
        back.push_back(add_vertex(data, {point[0], point[1], -depth * 0.5}));
        front.push_back(add_vertex(data, {point[0], point[1], depth * 0.5}));
    }
    add_face(data, front);
    auto reverse = back;
    std::reverse(reverse.begin(), reverse.end());
    add_face(data, reverse);
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const auto next = (i + 1) % points.size();
        add_face(data, {back[i], back[next], front[next], front[i]});
    }
}

void cylinder(schema::EditableMeshDataT& data, const MeshPrimitiveOptions& options, bool cone)
{
    std::vector<Id> bottom;
    std::vector<Id> top;
    const auto count = options.segments;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const auto angle = 2.0 * pi * static_cast<double>(i) / count;
        bottom.push_back(
            add_vertex(data, {options.radius * std::cos(angle), -options.size.y() * 0.5,
                              options.radius * std::sin(angle)}));
        if (!cone)
        {
            top.push_back(
                add_vertex(data, {options.radius * std::cos(angle), options.size.y() * 0.5,
                                  options.radius * std::sin(angle)}));
        }
    }
    const Id apex = cone ? add_vertex(data, {0, options.size.y() * 0.5, 0}) : 0;
    add_face(data, bottom);
    if (!cone)
    {
        auto reversed = top;
        std::reverse(reversed.begin(), reversed.end());
        add_face(data, reversed);
    }
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const auto next = (i + 1) % count;
        auto& face = cone ? add_face(data, {bottom[i], apex, bottom[next]}, 0, 1)
                          : add_face(data, {bottom[i], top[i], top[next], bottom[next]}, 0, 1);
        const auto u0 = static_cast<double>(i) / count;
        const auto u1 = static_cast<double>(i + 1) / count;
        if (cone)
        {
            face.texcoords = {{u0, 0}, {(u0 + u1) * 0.5, 1}, {u1, 0}};
        }
        else
        {
            face.texcoords = {{u0, 0}, {u0, 1}, {u1, 1}, {u1, 0}};
        }
    }
}

void sphere(schema::EditableMeshDataT& data, const MeshPrimitiveOptions& options)
{
    const auto count = options.segments;
    const auto rings = options.rings;
    const auto north = add_vertex(data, {0, options.radius, 0});
    const auto south = add_vertex(data, {0, -options.radius, 0});
    std::vector<std::vector<Id>> rows;
    for (std::uint32_t r = 1; r < rings; ++r)
    {
        const auto latitude = pi * static_cast<double>(r) / rings;
        std::vector<Id> row;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto longitude = 2.0 * pi * static_cast<double>(i) / count;
            row.push_back(
                add_vertex(data, {options.radius * std::sin(latitude) * std::cos(longitude),
                                  options.radius * std::cos(latitude),
                                  options.radius * std::sin(latitude) * std::sin(longitude)}));
        }
        rows.push_back(std::move(row));
    }
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const auto next = (i + 1) % count;
        const auto u0 = static_cast<double>(i) / count;
        const auto u1 = static_cast<double>(i + 1) / count;
        auto& top = add_face(data, {north, rows.front()[next], rows.front()[i]}, 0, 1);
        top.texcoords = {{(u0 + u1) * 0.5, 1}, {u1, 1.0 - 1.0 / rings}, {u0, 1.0 - 1.0 / rings}};
        auto& bottom = add_face(data, {rows.back()[i], rows.back()[next], south}, 0, 1);
        bottom.texcoords = {{u0, 1.0 / rings}, {u1, 1.0 / rings}, {(u0 + u1) * 0.5, 0}};
        for (std::size_t r = 0; r + 1 < rows.size(); ++r)
        {
            auto& face = add_face(
                data, {rows[r][i], rows[r][next], rows[r + 1][next], rows[r + 1][i]}, 0, 1);
            const auto v0 = 1.0 - static_cast<double>(r + 1) / rings;
            const auto v1 = 1.0 - static_cast<double>(r + 2) / rings;
            face.texcoords = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        }
    }
}

void torus(schema::EditableMeshDataT& data, const MeshPrimitiveOptions& options)
{
    std::vector<std::vector<Id>> rows;
    for (std::uint32_t i = 0; i < options.segments; ++i)
    {
        const auto theta = 2.0 * pi * static_cast<double>(i) / options.segments;
        std::vector<Id> row;
        for (std::uint32_t j = 0; j < options.rings; ++j)
        {
            const auto phi = 2.0 * pi * static_cast<double>(j) / options.rings;
            const auto radial = options.radius + options.inner_radius * std::cos(phi);
            row.push_back(
                add_vertex(data, {radial * std::cos(theta), options.inner_radius * std::sin(phi),
                                  radial * std::sin(theta)}));
        }
        rows.push_back(std::move(row));
    }
    for (std::uint32_t i = 0; i < options.segments; ++i)
    {
        const auto next_i = (i + 1) % options.segments;
        for (std::uint32_t j = 0; j < options.rings; ++j)
        {
            const auto next_j = (j + 1) % options.rings;
            auto& face = add_face(
                data, {rows[i][j], rows[i][next_j], rows[next_i][next_j], rows[next_i][j]}, 0, 1);
            const auto u0 = static_cast<double>(i) / options.segments;
            const auto u1 = static_cast<double>(i + 1) / options.segments;
            const auto v0 = static_cast<double>(j) / options.rings;
            const auto v1 = static_cast<double>(j + 1) / options.rings;
            face.texcoords = {{u0, v0}, {u0, v1}, {u1, v1}, {u1, v0}};
        }
    }
}

void pipe(schema::EditableMeshDataT& data, const MeshPrimitiveOptions& options)
{
    std::vector<Id> outer_bottom;
    std::vector<Id> outer_top;
    std::vector<Id> inner_bottom;
    std::vector<Id> inner_top;
    for (std::uint32_t i = 0; i < options.segments; ++i)
    {
        const auto angle = 2.0 * pi * static_cast<double>(i) / options.segments;
        const auto c = std::cos(angle);
        const auto s = std::sin(angle);
        const auto height = options.size.y() * 0.5;
        outer_bottom.push_back(add_vertex(data, {options.radius * c, -height, options.radius * s}));
        outer_top.push_back(add_vertex(data, {options.radius * c, height, options.radius * s}));
        inner_bottom.push_back(
            add_vertex(data, {options.inner_radius * c, -height, options.inner_radius * s}));
        inner_top.push_back(
            add_vertex(data, {options.inner_radius * c, height, options.inner_radius * s}));
    }
    for (std::uint32_t i = 0; i < options.segments; ++i)
    {
        const auto next = (i + 1) % options.segments;
        add_face(data, {outer_bottom[i], outer_top[i], outer_top[next], outer_bottom[next]}, 0, 1);
        add_face(data, {inner_bottom[i], inner_bottom[next], inner_top[next], inner_top[i]}, 0, 2);
        add_face(data, {outer_top[i], inner_top[i], inner_top[next], outer_top[next]});
        add_face(data, {outer_bottom[i], outer_bottom[next], inner_bottom[next], inner_bottom[i]});
    }
}

void curved_stairs(schema::EditableMeshDataT& data, const MeshPrimitiveOptions& options)
{
    std::map<std::tuple<std::uint32_t, std::uint32_t, bool>, Id> vertices;
    const auto vertex = [&](std::uint32_t boundary, std::uint32_t height, bool outer)
    {
        const auto key = std::make_tuple(boundary, height, outer);
        const auto found = vertices.find(key);
        if (found != vertices.end())
        {
            return found->second;
        }
        const auto angle = options.angle_degrees * pi / 180.0 * boundary / options.steps;
        const auto radius = outer ? options.radius : options.inner_radius;
        const auto id = add_vertex(
            data, {radius * std::cos(angle),
                   (static_cast<double>(height) / options.steps - 0.5) * options.size.y(),
                   radius * std::sin(angle)});
        vertices.emplace(key, id);
        return id;
    };
    for (std::uint32_t i = 0; i < options.steps; ++i)
    {
        const auto height = i + 1;
        add_face(data, {vertex(i, 0, true), vertex(i + 1, 0, true), vertex(i + 1, 0, false),
                        vertex(i, 0, false)});
        add_face(data, {vertex(i, height, false), vertex(i + 1, height, false),
                        vertex(i + 1, height, true), vertex(i, height, true)});
        std::vector<Id> outer{vertex(i, 0, true)};
        if (i > 0)
        {
            outer.push_back(vertex(i, i, true));
        }
        outer.push_back(vertex(i, height, true));
        outer.push_back(vertex(i + 1, height, true));
        outer.push_back(vertex(i + 1, 0, true));
        add_face(data, outer);
        std::vector<Id> inner{vertex(i, 0, false), vertex(i + 1, 0, false),
                              vertex(i + 1, height, false), vertex(i, height, false)};
        if (i > 0)
        {
            inner.push_back(vertex(i, i, false));
        }
        add_face(data, inner);
        add_face(data, {vertex(i, i, false), vertex(i, height, false), vertex(i, height, true),
                        vertex(i, i, true)});
    }
    const auto end = options.steps;
    add_face(data, {vertex(end, 0, false), vertex(end, 0, true), vertex(end, end, true),
                    vertex(end, end, false)});
}

void icosphere(schema::EditableMeshDataT& data, const MeshPrimitiveOptions& options)
{
    const auto golden = (1.0 + std::sqrt(5.0)) * 0.5;
    std::vector<Id> vertices;
    for (const auto& point : std::vector<Vector>{{-1, golden, 0},
                                                 {1, golden, 0},
                                                 {-1, -golden, 0},
                                                 {1, -golden, 0},
                                                 {0, -1, golden},
                                                 {0, 1, golden},
                                                 {0, -1, -golden},
                                                 {0, 1, -golden},
                                                 {golden, 0, -1},
                                                 {golden, 0, 1},
                                                 {-golden, 0, -1},
                                                 {-golden, 0, 1}})
    {
        vertices.push_back(add_vertex(data, mul(normalized(point), options.radius)));
    }
    std::vector<std::array<Id, 3>> triangles;
    for (const auto& indices : std::vector<std::array<std::size_t, 3>>{
             {0, 11, 5},  {0, 5, 1},  {0, 1, 7},  {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
             {11, 10, 2}, {10, 7, 6}, {7, 1, 8},  {3, 9, 4},  {3, 4, 2},   {3, 2, 6}, {3, 6, 8},
             {3, 8, 9},   {4, 9, 5},  {2, 4, 11}, {6, 2, 10}, {8, 6, 7},   {9, 8, 1}})
    {
        triangles.push_back({vertices[indices[0]], vertices[indices[1]], vertices[indices[2]]});
    }
    for (std::uint32_t iteration = 0; iteration < options.subdivisions; ++iteration)
    {
        std::map<std::pair<Id, Id>, Id> midpoints;
        const auto midpoint = [&](Id a, Id b)
        {
            const auto key = std::minmax(a, b);
            const auto found = midpoints.find(key);
            if (found != midpoints.end())
            {
                return found->second;
            }
            const auto id = add_vertex(
                data, mul(normalized(add(position(data, a), position(data, b))), options.radius));
            midpoints.emplace(key, id);
            return id;
        };
        std::vector<std::array<Id, 3>> subdivided;
        for (const auto& triangle : triangles)
        {
            const auto ab = midpoint(triangle[0], triangle[1]);
            const auto bc = midpoint(triangle[1], triangle[2]);
            const auto ca = midpoint(triangle[2], triangle[0]);
            subdivided.push_back({triangle[0], ab, ca});
            subdivided.push_back({triangle[1], bc, ab});
            subdivided.push_back({triangle[2], ca, bc});
            subdivided.push_back({ab, bc, ca});
        }
        triangles = std::move(subdivided);
    }
    for (const auto& triangle : triangles)
    {
        auto& face = add_face(data, {triangle[0], triangle[1], triangle[2]}, 0, 1);
        double minimum = 1.0;
        double maximum = 0.0;
        for (auto id : triangle)
        {
            const auto normal = normalized(position(data, id));
            const auto u = std::atan2(normal[2], normal[0]) / (2.0 * pi) + 0.5;
            const auto v = std::asin(std::clamp(normal[1], -1.0, 1.0)) / pi + 0.5;
            minimum = std::min(minimum, u);
            maximum = std::max(maximum, u);
            face.texcoords.emplace_back(u, v);
        }
        if (maximum - minimum > 0.5)
        {
            for (auto& uv : face.texcoords)
            {
                if (uv.x() < 0.5)
                {
                    uv = {uv.x() + 1.0, uv.y()};
                }
            }
        }
    }
}

} // namespace

bool MeshPrimitives::create(const MeshPrimitiveOptions& options, schema::EditableMeshDataT& output,
                            std::string& error)
{
    output = {};
    error.clear();
    const Vector size{options.size.x(), options.size.y(), options.size.z()};
    if (!finite(size) || size[0] <= epsilon || size[1] <= epsilon || size[2] <= epsilon ||
        !std::isfinite(options.radius) || options.radius <= epsilon ||
        !std::isfinite(options.inner_radius) || options.inner_radius <= epsilon ||
        options.segments < 3 || options.segments > 256 || options.rings < 3 ||
        options.rings > 256 || options.steps < 1 || options.steps > 256 ||
        options.width_segments < 1 || options.width_segments > 256 || options.depth_segments < 1 ||
        options.depth_segments > 256 || !std::isfinite(options.angle_degrees) ||
        !std::isfinite(options.thickness) || options.thickness <= epsilon ||
        options.subdivisions > 5)
    {
        return fail(error, "基本体参数无效：尺寸和半径须为正值，分段数量不得超过 256");
    }
    schema::EditableMeshDataT result;
    switch (options.type)
    {
    case MeshPrimitive::cube:
        box(result, size);
        break;
    case MeshPrimitive::plane:
    {
        std::vector<std::vector<Id>> rows;
        for (std::uint32_t z = 0; z <= options.depth_segments; ++z)
        {
            std::vector<Id> row;
            for (std::uint32_t x = 0; x <= options.width_segments; ++x)
            {
                row.push_back(add_vertex(
                    result, {(static_cast<double>(x) / options.width_segments - 0.5) * size[0], 0,
                             (static_cast<double>(z) / options.depth_segments - 0.5) * size[2]}));
            }
            rows.push_back(std::move(row));
        }
        for (std::uint32_t z = 0; z < options.depth_segments; ++z)
        {
            for (std::uint32_t x = 0; x < options.width_segments; ++x)
            {
                auto& face = add_face(
                    result, {rows[z][x], rows[z + 1][x], rows[z + 1][x + 1], rows[z][x + 1]});
                const auto u0 = static_cast<double>(x) / options.width_segments;
                const auto u1 = static_cast<double>(x + 1) / options.width_segments;
                const auto v0 = static_cast<double>(z) / options.depth_segments;
                const auto v1 = static_cast<double>(z + 1) / options.depth_segments;
                face.texcoords = {{u0, v0}, {u0, v1}, {u1, v1}, {u1, v0}};
            }
        }
        break;
    }
    case MeshPrimitive::cylinder:
        cylinder(result, options, false);
        break;
    case MeshPrimitive::cone:
        cylinder(result, options, true);
        break;
    case MeshPrimitive::sphere:
        sphere(result, options);
        break;
    case MeshPrimitive::torus:
        if (options.inner_radius >= options.radius)
        {
            return fail(error, "环面管半径必须小于主半径");
        }
        torus(result, options);
        break;
    case MeshPrimitive::prism:
        extruded_outline(
            result,
            {{-size[0] * 0.5, -size[1] * 0.5}, {size[0] * 0.5, -size[1] * 0.5}, {0, size[1] * 0.5}},
            size[2]);
        break;
    case MeshPrimitive::stairs:
    {
        std::vector<Point> outline{{-size[0] * 0.5, -size[1] * 0.5},
                                   {size[0] * 0.5, -size[1] * 0.5},
                                   {size[0] * 0.5, size[1] * 0.5}};
        for (std::uint32_t i = options.steps; i > 0; --i)
        {
            const auto x = (static_cast<double>(i - 1) / options.steps - 0.5) * size[0];
            outline.push_back({x, (static_cast<double>(i) / options.steps - 0.5) * size[1]});
            if (i > 1)
            {
                outline.push_back(
                    {x, (static_cast<double>(i - 1) / options.steps - 0.5) * size[1]});
            }
        }
        extruded_outline(result, outline, size[2]);
        break;
    }
    case MeshPrimitive::arch:
    {
        if (options.inner_radius >= options.radius || options.angle_degrees <= 0.0 ||
            options.angle_degrees >= 360.0)
        {
            return fail(error, "拱体需要内径小于外径，角度在 0 到 360 度之间");
        }
        std::vector<Point> outline;
        for (std::uint32_t i = 0; i <= options.segments; ++i)
        {
            const auto angle =
                options.angle_degrees * pi / 180.0 * static_cast<double>(i) / options.segments;
            outline.push_back({options.radius * std::cos(angle), options.radius * std::sin(angle)});
        }
        for (std::uint32_t i = options.segments + 1; i > 0; --i)
        {
            const auto angle =
                options.angle_degrees * pi / 180.0 * static_cast<double>(i - 1) / options.segments;
            outline.push_back(
                {options.inner_radius * std::cos(angle), options.inner_radius * std::sin(angle)});
        }
        extruded_outline(result, outline, size[2]);
        break;
    }
    case MeshPrimitive::pipe:
        if (options.inner_radius >= options.radius)
        {
            return fail(error, "管道内半径必须小于外半径");
        }
        pipe(result, options);
        break;
    case MeshPrimitive::door:
    {
        const auto thickness = options.thickness;
        const auto x = size[0] * 0.5;
        const auto y = size[1] * 0.5;
        if (thickness >= x || thickness >= size[1])
        {
            return fail(error, "门框厚度必须小于半宽与高度");
        }
        extruded_outline(result,
                         {{-x, -y},
                          {-x + thickness, -y},
                          {-x + thickness, y - thickness},
                          {x - thickness, y - thickness},
                          {x - thickness, -y},
                          {x, -y},
                          {x, y},
                          {-x, y}},
                         size[2]);
        break;
    }
    case MeshPrimitive::curved_stairs:
        if (options.inner_radius >= options.radius || options.angle_degrees <= 0.0 ||
            options.angle_degrees >= 360.0 || options.angle_degrees / options.steps >= 180.0)
        {
            return fail(error, "弧形楼梯要求内径小于外径，角度在0到360度且单阶小于180度");
        }
        curved_stairs(result, options);
        break;
    case MeshPrimitive::sprite:
    {
        std::vector<Id> ids;
        for (const auto& point : std::vector<Vector>{{-size[0] * 0.5, -size[1] * 0.5, 0},
                                                     {size[0] * 0.5, -size[1] * 0.5, 0},
                                                     {size[0] * 0.5, size[1] * 0.5, 0},
                                                     {-size[0] * 0.5, size[1] * 0.5, 0}})
        {
            ids.push_back(add_vertex(result, point));
        }
        add_face(result, ids).texcoords = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        break;
    }
    case MeshPrimitive::icosphere:
        icosphere(result, options);
        break;
    default:
        return fail(error, "基本体类型未知");
    }
    if (!EditableMesh::validate(result, error))
    {
        return false;
    }
    output = std::move(result);
    return true;
}

bool MeshPrimitives::polygon(std::span<const schema::Vec3> points,
                             const std::vector<std::vector<schema::Vec3>>& holes,
                             schema::EditableMeshDataT& output, std::string& error)
{
    output = {};
    error.clear();
    if (points.size() < 3 || points.size() > 65536 || holes.size() > 1024)
    {
        return fail(error, "多边形外环至少需要 3 个点，且不得超过模型限制");
    }
    schema::EditableMeshDataT result;
    std::vector<Id> outer;
    for (const auto& point : points)
    {
        outer.push_back(add_vertex(result, {point.x(), point.y(), point.z()}));
    }
    auto& face = add_face(result, outer);
    for (const auto& points_in_hole : holes)
    {
        auto hole = std::make_unique<schema::ModelingLoopT>();
        for (const auto& point : points_in_hole)
        {
            hole->vertices.push_back(add_vertex(result, {point.x(), point.y(), point.z()}));
        }
        face.holes.push_back(std::move(hole));
    }
    if (!EditableMesh::validate(result, error))
    {
        return false;
    }
    output = std::move(result);
    return true;
}

} // namespace engine
