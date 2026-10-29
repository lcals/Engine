#include "plugins/editor/modeling/mesh_geometry.hpp"

#include "plugins/editor/modeling/mesh_geometry_internal.hpp"

#include <functional>
#include <queue>

namespace engine
{
namespace
{

using namespace modeling_detail;

template <typename Function> void map_loops(schema::ModelingFaceT& face, Function&& function)
{
    function(face.vertices, face.texcoords);
    for (auto& hole : face.holes)
    {
        function(hole->vertices, hole->texcoords);
    }
}

bool finite_uv(const schema::Vec2& value)
{
    return std::isfinite(value.x()) && std::isfinite(value.y());
}

struct UvBounds
{
    double min_u = std::numeric_limits<double>::max();
    double min_v = std::numeric_limits<double>::max();
    double max_u = std::numeric_limits<double>::lowest();
    double max_v = std::numeric_limits<double>::lowest();
};

bool face_projection(schema::EditableMeshDataT& data, schema::ModelingFaceT& face, UvBounds& bounds,
                     std::string& error)
{
    Point normal;
    if (!face_normal(data, face, normal))
    {
        error = "Cannot project a degenerate face";
        return false;
    }
    Point u;
    Point v;
    basis(normal, u, v);
    bounds = {};
    map_loops(face,
              [&](const auto& vertices, auto& texcoords)
              {
                  texcoords.clear();
                  for (const auto id : vertices)
                  {
                      const Point p = position(data, id);
                      const double x = dot(p, u);
                      const double y = dot(p, v);
                      bounds.min_u = std::min(bounds.min_u, x);
                      bounds.min_v = std::min(bounds.min_v, y);
                      bounds.max_u = std::max(bounds.max_u, x);
                      bounds.max_v = std::max(bounds.max_v, y);
                      texcoords.emplace_back(x, y);
                  }
              });
    return true;
}

} // namespace

bool MeshGeometry::project_uv(schema::EditableMeshDataT& data,
                              std::span<const std::uint64_t> face_ids,
                              const MeshUvProjectionOptions& options, std::string& error)
{
    if (options.projection != MeshUvProjection::planar &&
        options.projection != MeshUvProjection::box &&
        options.projection != MeshUvProjection::cylindrical &&
        options.projection != MeshUvProjection::spherical)
    {
        error = "Unknown UV projection";
        return false;
    }
    Point axis = point(options.normal);
    if (!normalize(axis) || !finite_uv(options.scale) || !finite_uv(options.offset))
    {
        error = "Invalid UV projection axis, scale, or offset";
        return false;
    }
    schema::EditableMeshDataT candidate = data;
    std::vector<schema::ModelingFaceT*> faces;
    if (!selected_faces(candidate, face_ids, faces, error))
    {
        return false;
    }
    Point u;
    Point v;
    basis(axis, u, v);
    for (auto* face : faces)
    {
        Point face_u = u;
        Point face_v = v;
        if (options.projection == MeshUvProjection::box)
        {
            Point normal;
            if (!face_normal(candidate, *face, normal))
            {
                error = "Cannot project a degenerate face";
                return false;
            }
            const double ax = std::abs(normal.x);
            const double ay = std::abs(normal.y);
            const double az = std::abs(normal.z);
            Point major;
            if (ax >= ay && ax >= az)
            {
                major = {normal.x < 0.0 ? -1.0 : 1.0, 0.0, 0.0};
            }
            else if (ay >= az)
            {
                major = {0.0, normal.y < 0.0 ? -1.0 : 1.0, 0.0};
            }
            else
            {
                major = {0.0, 0.0, normal.z < 0.0 ? -1.0 : 1.0};
            }
            basis(major, face_u, face_v);
        }
        bool valid = true;
        map_loops(*face,
                  [&](const auto& vertices, auto& texcoords)
                  {
                      texcoords.clear();
                      for (const auto id : vertices)
                      {
                          const Point p = position(candidate, id);
                          double x = dot(p, face_u);
                          double y = dot(p, face_v);
                          if (options.projection == MeshUvProjection::cylindrical ||
                              options.projection == MeshUvProjection::spherical)
                          {
                              x = std::atan2(dot(p, v), dot(p, u)) / (2.0 * pi) + 0.5;
                              y = dot(p, axis);
                              if (options.projection == MeshUvProjection::spherical)
                              {
                                  const double radius = length(p);
                                  if (radius <= epsilon)
                                  {
                                      valid = false;
                                      return;
                                  }
                                  y = std::acos(std::clamp(y / radius, -1.0, 1.0)) / pi;
                              }
                          }
                          texcoords.emplace_back(x, y);
                      }
                      if (options.projection == MeshUvProjection::cylindrical ||
                          options.projection == MeshUvProjection::spherical)
                      {
                          double min_u = 1.0;
                          double max_u = 0.0;
                          for (const auto& uv : texcoords)
                          {
                              min_u = std::min(min_u, uv.x());
                              max_u = std::max(max_u, uv.x());
                          }
                          if (max_u - min_u > 0.5)
                          {
                              for (auto& uv : texcoords)
                              {
                                  if (uv.x() < 0.5)
                                  {
                                      uv = schema::Vec2(uv.x() + 1.0, uv.y());
                                  }
                              }
                          }
                      }
                      for (auto& uv : texcoords)
                      {
                          uv = schema::Vec2(uv.x() * options.scale.x() + options.offset.x(),
                                            uv.y() * options.scale.y() + options.offset.y());
                      }
                  });
        if (!valid)
        {
            error = "Spherical projection is undefined at its origin";
            return false;
        }
    }
    return commit(data, std::move(candidate), error);
}

bool MeshGeometry::transform_uv(schema::EditableMeshDataT& data,
                                std::span<const std::uint64_t> face_ids,
                                const MeshUvTransformOptions& options, std::string& error)
{
    if (!finite_uv(options.scale) || !finite_uv(options.offset) || !finite_uv(options.pivot) ||
        !std::isfinite(options.rotation_degrees))
    {
        error = "Invalid UV transform";
        return false;
    }
    schema::EditableMeshDataT candidate = data;
    std::vector<schema::ModelingFaceT*> faces;
    if (!selected_faces(candidate, face_ids, faces, error))
    {
        return false;
    }
    const double angle = options.rotation_degrees * pi / 180.0;
    for (auto* face : faces)
    {
        bool valid = true;
        map_loops(*face,
                  [&](const auto& vertices, auto& texcoords)
                  {
                      if (texcoords.size() != vertices.size())
                      {
                          valid = false;
                          return;
                      }
                      for (auto& uv : texcoords)
                      {
                          const double x = (uv.x() - options.pivot.x()) * options.scale.x();
                          const double y = (uv.y() - options.pivot.y()) * options.scale.y();
                          uv = schema::Vec2(x * std::cos(angle) - y * std::sin(angle) +
                                                options.pivot.x() + options.offset.x(),
                                            x * std::sin(angle) + y * std::cos(angle) +
                                                options.pivot.y() + options.offset.y());
                      }
                  });
        if (!valid)
        {
            error = "UV transform requires texture coordinates on every selected corner";
            return false;
        }
    }
    return commit(data, std::move(candidate), error);
}

bool MeshGeometry::unwrap_face_uv(schema::EditableMeshDataT& data,
                                  std::span<const std::uint64_t> face_ids,
                                  const MeshUvUnwrapOptions& options, std::string& error)
{
    if (!std::isfinite(options.padding) || options.padding < 0.0 || options.padding >= 0.5)
    {
        error = "UV padding must be finite and within [0, 0.5)";
        return false;
    }
    schema::EditableMeshDataT candidate = data;
    std::vector<schema::ModelingFaceT*> faces;
    if (!selected_faces(candidate, face_ids, faces, error))
    {
        return false;
    }
    const auto columns = static_cast<std::size_t>(std::ceil(std::sqrt(faces.size())));
    const auto rows = (faces.size() + columns - 1) / columns;
    const double cell_width = 1.0 / static_cast<double>(columns);
    const double cell_height = 1.0 / static_cast<double>(rows);
    if (options.padding * 2.0 >= std::min(cell_width, cell_height))
    {
        error = "UV padding leaves no space for the requested islands";
        return false;
    }
    for (std::size_t i = 0; i < faces.size(); ++i)
    {
        UvBounds bounds;
        if (!face_projection(candidate, *faces[i], bounds, error))
        {
            return false;
        }
        const double width = bounds.max_u - bounds.min_u;
        const double height = bounds.max_v - bounds.min_v;
        if (width <= epsilon || height <= epsilon)
        {
            error = "Cannot pack a degenerate UV island";
            return false;
        }
        const double scale = std::min((cell_width - 2.0 * options.padding) / width,
                                      (cell_height - 2.0 * options.padding) / height);
        const double x = (i % columns) * cell_width + (cell_width - width * scale) * 0.5;
        const double y = (i / columns) * cell_height + (cell_height - height * scale) * 0.5;
        map_loops(*faces[i],
                  [&](const auto&, auto& texcoords)
                  {
                      for (auto& uv : texcoords)
                      {
                          uv = schema::Vec2(x + (uv.x() - bounds.min_u) * scale,
                                            y + (uv.y() - bounds.min_v) * scale);
                      }
                  });
    }
    return commit(data, std::move(candidate), error);
}

bool MeshGeometry::hotspot_uv(schema::EditableMeshDataT& data,
                              std::span<const std::uint64_t> face_ids,
                              const MeshHotspotOptions& options, std::string& error)
{
    if (options.rectangles.empty() || options.rectangles.size() > 65536 ||
        !std::isfinite(options.padding) || options.padding < 0.0)
    {
        error = "Hotspot mapping requires a valid atlas rectangle list and padding";
        return false;
    }
    for (const auto& rectangle : options.rectangles)
    {
        if (!std::isfinite(rectangle.x()) || !std::isfinite(rectangle.y()) ||
            !std::isfinite(rectangle.z()) || !std::isfinite(rectangle.w()) || rectangle.x() < 0.0 ||
            rectangle.y() < 0.0 || rectangle.z() <= 2.0 * options.padding ||
            rectangle.w() <= 2.0 * options.padding || rectangle.x() + rectangle.z() > 1.0 ||
            rectangle.y() + rectangle.w() > 1.0)
        {
            error = "Hotspot rectangles must fit the unit atlas and leave padding space";
            return false;
        }
    }
    schema::EditableMeshDataT candidate = data;
    std::vector<schema::ModelingFaceT*> faces;
    if (!selected_faces(candidate, face_ids, faces, error))
    {
        return false;
    }
    for (auto* face : faces)
    {
        UvBounds bounds;
        if (!face_projection(candidate, *face, bounds, error))
        {
            return false;
        }
        const double width = bounds.max_u - bounds.min_u;
        const double height = bounds.max_v - bounds.min_v;
        if (width <= epsilon || height <= epsilon)
        {
            error = "Cannot map a degenerate Hotspot face";
            return false;
        }
        double best_score = std::numeric_limits<double>::max();
        std::size_t best_index = 0;
        bool best_rotated = false;
        for (std::size_t i = 0; i < options.rectangles.size(); ++i)
        {
            const auto& rectangle = options.rectangles[i];
            const double aspect =
                (rectangle.z() - 2.0 * options.padding) / (rectangle.w() - 2.0 * options.padding);
            for (int rotation = 0; rotation <= (options.allow_rotation ? 1 : 0); ++rotation)
            {
                const double face_aspect = rotation == 0 ? width / height : height / width;
                const double score = std::abs(std::log(face_aspect / aspect));
                if (score < best_score)
                {
                    best_score = score;
                    best_index = i;
                    best_rotated = rotation == 1;
                }
            }
        }
        const auto& rectangle = options.rectangles[best_index];
        map_loops(*face,
                  [&](const auto&, auto& texcoords)
                  {
                      for (auto& uv : texcoords)
                      {
                          double x = (uv.x() - bounds.min_u) / width;
                          double y = (uv.y() - bounds.min_v) / height;
                          if (best_rotated)
                          {
                              const double previous = x;
                              x = 1.0 - y;
                              y = previous;
                          }
                          uv = schema::Vec2(rectangle.x() + options.padding +
                                                x * (rectangle.z() - 2.0 * options.padding),
                                            rectangle.y() + options.padding +
                                                y * (rectangle.w() - 2.0 * options.padding));
                      }
                  });
    }
    return commit(data, std::move(candidate), error);
}

bool MeshGeometry::set_smoothing(schema::EditableMeshDataT& data,
                                 std::span<const std::uint64_t> face_ids, std::uint32_t group,
                                 std::string& error)
{
    schema::EditableMeshDataT candidate = data;
    std::vector<schema::ModelingFaceT*> faces;
    if (!selected_faces(candidate, face_ids, faces, error))
    {
        return false;
    }
    for (auto* face : faces)
    {
        face->smoothing_group = group;
    }
    return commit(data, std::move(candidate), error);
}

bool MeshGeometry::auto_smooth(schema::EditableMeshDataT& data, double angle_degrees,
                               std::string& error)
{
    error.clear();
    if (!std::isfinite(angle_degrees) || angle_degrees < 0.0 || angle_degrees > 180.0 ||
        !EditableMesh::validate(data, error))
    {
        if (error.empty())
        {
            error = "Smoothing angle must be within [0, 180] degrees";
        }
        return false;
    }
    schema::EditableMeshDataT candidate = data;
    std::vector<Point> normals(candidate.faces.size());
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<std::size_t>> edges;
    for (std::size_t i = 0; i < candidate.faces.size(); ++i)
    {
        auto& face = *candidate.faces[i];
        if (!face_normal(candidate, face, normals[i]))
        {
            error = "Cannot smooth a degenerate face";
            return false;
        }
        face.smoothing_group = 0;
        map_loops(
            face,
            [&](const auto& vertices, auto&)
            {
                for (std::size_t j = 0; j < vertices.size(); ++j)
                {
                    edges[edge_key(vertices[j], vertices[(j + 1) % vertices.size()])].push_back(i);
                }
            });
    }
    std::vector<std::vector<std::size_t>> neighbors(candidate.faces.size());
    const double cosine = std::cos(angle_degrees * pi / 180.0);
    for (const auto& [edge, faces] : edges)
    {
        (void)edge;
        if (faces.size() == 2 && dot(normals[faces[0]], normals[faces[1]]) >= cosine - epsilon)
        {
            neighbors[faces[0]].push_back(faces[1]);
            neighbors[faces[1]].push_back(faces[0]);
        }
    }
    std::uint32_t group = 0;
    for (std::size_t i = 0; i < candidate.faces.size(); ++i)
    {
        if (candidate.faces[i]->smoothing_group != 0)
        {
            continue;
        }
        ++group;
        std::queue<std::size_t> pending;
        candidate.faces[i]->smoothing_group = group;
        pending.push(i);
        while (!pending.empty())
        {
            const auto current = pending.front();
            pending.pop();
            for (const auto neighbor : neighbors[current])
            {
                if (candidate.faces[neighbor]->smoothing_group == 0)
                {
                    candidate.faces[neighbor]->smoothing_group = group;
                    pending.push(neighbor);
                }
            }
        }
    }
    return commit(data, std::move(candidate), error);
}

} // namespace engine
