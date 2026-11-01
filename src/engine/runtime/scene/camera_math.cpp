#include "runtime/scene/camera_math.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace engine::camera_detail
{
namespace
{

bool finite_matrix(const float* matrix) noexcept
{
    return std::all_of(matrix, matrix + 16, [](float value) { return std::isfinite(value); });
}

} // namespace

bool view_matrix(const float* world, float* view) noexcept
{
    if (!finite_matrix(world))
    {
        return false;
    }
    // 用节点 -Z 朝向和 +Y 上方向建立正交相机基；缩放大小不改变镜头投影。
    const double back_length =
        std::hypot(static_cast<double>(world[8]), static_cast<double>(world[9]),
                   static_cast<double>(world[10]));
    if (!(back_length > 0.0))
    {
        return false;
    }
    const std::array<double, 3> back = {world[8] / back_length, world[9] / back_length,
                                        world[10] / back_length};
    std::array<double, 3> right = {world[5] * back[2] - world[6] * back[1],
                                   world[6] * back[0] - world[4] * back[2],
                                   world[4] * back[1] - world[5] * back[0]};
    const double right_length = std::hypot(right[0], right[1], right[2]);
    // 即使 +Y/-Z 仍可确定方向，退化的世界 X 轴也不是有效相机变换。
    const double volume = right[0] * world[0] + right[1] * world[1] + right[2] * world[2];
    if (!(right_length > 0.0) || volume == 0.0)
    {
        return false;
    }
    for (double& value : right)
    {
        value /= right_length;
    }
    const std::array<double, 3> up = {back[1] * right[2] - back[2] * right[1],
                                      back[2] * right[0] - back[0] * right[2],
                                      back[0] * right[1] - back[1] * right[0]};
    for (std::size_t column = 0; column < 3; ++column)
    {
        view[column * 4] = static_cast<float>(right[column]);
        view[column * 4 + 1] = static_cast<float>(up[column]);
        view[column * 4 + 2] = static_cast<float>(back[column]);
        view[column * 4 + 3] = 0.0F;
    }
    view[12] =
        static_cast<float>(-(right[0] * world[12] + right[1] * world[13] + right[2] * world[14]));
    view[13] = static_cast<float>(-(up[0] * world[12] + up[1] * world[13] + up[2] * world[14]));
    view[14] =
        static_cast<float>(-(back[0] * world[12] + back[1] * world[13] + back[2] * world[14]));
    view[15] = 1.0F;
    return finite_matrix(view);
}

bool projection_matrix(const CameraSettings& settings, double aspect, bool homogeneous_depth,
                       float* projection) noexcept
{
    if (!std::isfinite(aspect) || aspect <= 0.0 || aspect > std::numeric_limits<float>::max())
    {
        return false;
    }
    const float ratio = static_cast<float>(aspect);
    if (!(ratio > 0.0F))
    {
        return false;
    }
    if (settings.projection == schema::Projection::orthographic)
    {
        const float horizontal = settings.orthographic_size * ratio;
        bx::mtxOrtho(projection, -horizontal, horizontal, -settings.orthographic_size,
                     settings.orthographic_size, settings.near_clip, settings.far_clip, 0.0F,
                     homogeneous_depth, bx::Handedness::Right);
    }
    else if (settings.projection == schema::Projection::perspective)
    {
        bx::mtxProj(projection, settings.fov_degrees, ratio, settings.near_clip, settings.far_clip,
                    homogeneous_depth, bx::Handedness::Right);
    }
    else
    {
        return false;
    }
    return finite_matrix(projection) && projection[0] > 0.0F && projection[5] > 0.0F;
}

} // namespace engine::camera_detail
