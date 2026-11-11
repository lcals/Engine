#include "plugins/editor/modeling/primitive_options.hpp"

#include <array>
#include <cmath>

namespace engine::plugins::modeling::detail
{
namespace
{
using P = schema::ModelingPrimitive;
using D = schema::ModelingPrimitiveDataT;
constexpr std::uint32_t bit(P type)
{
    return 1U << static_cast<std::uint32_t>(type);
}
constexpr auto cube = bit(P::cube), plane = bit(P::plane), cylinder = bit(P::cylinder),
               cone = bit(P::cone), sphere = bit(P::sphere), torus = bit(P::torus),
               prism = bit(P::prism), stairs = bit(P::stairs), arch = bit(P::arch),
               pipe = bit(P::pipe), door = bit(P::door), curved = bit(P::curved_stairs),
               sprite = bit(P::sprite), ico = bit(P::icosphere);
constexpr auto all = (ico << 1U) - 1U;
constexpr double epsilon = 1.0e-9;
constexpr double extent = 1.0e12;
constexpr PrimitiveParameter parameters[] = {
    {"size", cube | prism | stairs | door, nullptr, nullptr, 3, epsilon, extent, true},
    {"width", plane | sprite, nullptr, nullptr, 0, epsilon, extent, true},
    {"height", cylinder | cone | pipe | curved | sprite, nullptr, nullptr, 1, epsilon, extent,
     true},
    {"depth", plane | arch, nullptr, nullptr, 2, epsilon, extent, true},
    {"radius", cylinder | cone | sphere | torus | arch | pipe | curved | ico, &D::radius, nullptr,
     -1, epsilon, extent, true},
    {"innerRadius", arch | pipe | curved, &D::inner_radius, nullptr, -1, epsilon, extent, true},
    {"tubeRadius", torus, &D::inner_radius, nullptr, -1, epsilon, extent, true},
    {"angleDegrees", arch | curved, &D::angle_degrees, nullptr, -1, 0, 360, true},
    {"thickness", door, &D::thickness, nullptr, -1, epsilon, extent, true},
    {"subdivisions", ico, nullptr, &D::subdivisions, -1, 0, 5},
    {"segments", cylinder | cone | sphere | torus | arch | pipe, nullptr, &D::segments, -1, 3, 256},
    {"rings", sphere | torus, nullptr, &D::rings, -1, 3, 256},
    {"steps", stairs | curved, nullptr, &D::steps, -1, 1, 256},
    {"widthSegments", plane, nullptr, &D::width_segments, -1, 1, 256},
    {"depthSegments", plane, nullptr, &D::depth_segments, -1, 1, 256},
    {"uvTilesPerUnit", all, &D::uv_tiles_per_unit, nullptr, -1, 0, extent, true}};

bool in_range(double value, const PrimitiveParameter& parameter)
{
    return std::isfinite(value) && value <= parameter.maximum &&
           (parameter.exclusive_minimum ? value > parameter.minimum : value >= parameter.minimum);
}
} // namespace

std::span<const PrimitiveParameter> primitive_parameters()
{
    return parameters;
}

bool supports(const PrimitiveParameter& parameter, P type)
{
    return type >= P::cube && type <= P::icosphere && (parameter.shapes & bit(type)) != 0;
}

bool validate_primitive(const D& options, std::string& error)
{
    error.clear();
    if (options.type < P::cube || options.type > P::icosphere)
    {
        error = "Unknown primitive type";
        return false;
    }
    D accepted;
    const std::array size{options.size.x(), options.size.y(), options.size.z()};
    std::array accepted_size{1.0, 1.0, 1.0};
    for (const auto& parameter : parameters)
    {
        if (!supports(parameter, options.type))
        {
            continue;
        }
        bool valid = true;
        if (parameter.scalar)
        {
            accepted.*parameter.scalar = options.*parameter.scalar;
            valid = in_range(options.*parameter.scalar, parameter);
        }
        else if (parameter.count)
        {
            accepted.*parameter.count = options.*parameter.count;
            valid = in_range(options.*parameter.count, parameter);
        }
        else
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                if (parameter.size_axis == 3 || parameter.size_axis == axis)
                {
                    accepted_size[axis] = size[axis];
                    valid = valid && in_range(size[axis], parameter);
                }
            }
        }
        if (!valid)
        {
            error = "Invalid primitive parameter: " + std::string(parameter.name);
            return false;
        }
    }
    // 未参与当前形状的字段必须保持 schema 默认值，不能静默接受无效参数。
    bool relevant = size == accepted_size;
    for (const auto& parameter : parameters)
    {
        if (parameter.scalar)
        {
            relevant = relevant && options.*parameter.scalar == accepted.*parameter.scalar;
        }
        else if (parameter.count)
        {
            relevant = relevant && options.*parameter.count == accepted.*parameter.count;
        }
    }
    if (!relevant)
    {
        error = "Unused primitive fields must retain their default values";
        return false;
    }
    if ((options.type == P::torus || options.type == P::arch || options.type == P::pipe ||
         options.type == P::curved_stairs) &&
        options.inner_radius >= options.radius)
    {
        error = "Inner/tube radius must be smaller than radius";
        return false;
    }
    if ((options.type == P::arch || options.type == P::curved_stairs) &&
        (options.angle_degrees >= 360 ||
         (options.type == P::curved_stairs && options.angle_degrees / options.steps >= 180)))
    {
        error = "Arc angle must be below 360 degrees; each curved step must be below 180 degrees";
        return false;
    }
    if (options.type == P::door &&
        (options.thickness >= options.size.x() * 0.5 || options.thickness >= options.size.y()))
    {
        error = "Door thickness must be smaller than half width and height";
        return false;
    }
    return true;
}
} // namespace engine::plugins::modeling::detail
