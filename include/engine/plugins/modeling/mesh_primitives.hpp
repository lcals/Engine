#pragma once

#include "plugins/modeling/editable_mesh.hpp"

namespace engine
{

enum class MeshPrimitive
{
    cube,
    plane,
    cylinder,
    cone,
    sphere,
    torus,
    prism,
    stairs,
    arch,
    pipe,
    door,
    curved_stairs,
    sprite,
    icosphere
};

struct MeshPrimitiveOptions
{
    MeshPrimitive type = MeshPrimitive::cube;
    schema::Vec3 size = {1.0, 1.0, 1.0};
    double radius = 0.5;
    double inner_radius = 0.25;
    double angle_degrees = 180.0;
    double thickness = 0.1;
    std::uint32_t subdivisions = 2;
    std::uint32_t segments = 24;
    std::uint32_t rings = 12;
    std::uint32_t steps = 8;
    std::uint32_t width_segments = 1;
    std::uint32_t depth_segments = 1;
};

class MeshPrimitives final
{
public:
    [[nodiscard]] static bool create(const MeshPrimitiveOptions& options,
                                     schema::EditableMeshDataT& output, std::string& error);
    // 以输入绕序为正面；洞与外环必须共面且不相交。
    [[nodiscard]] static bool polygon(std::span<const schema::Vec3> points,
                                      const std::vector<std::vector<schema::Vec3>>& holes,
                                      schema::EditableMeshDataT& output, std::string& error);
};

} // namespace engine
