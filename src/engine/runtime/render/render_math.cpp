#include "runtime/render/render_math.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>

namespace engine::render_detail
{

bool finite_matrix(const float* matrix)
{
    return std::all_of(matrix, matrix + 16, [](float value) { return std::isfinite(value); });
}

double linear_determinant(const float* matrix)
{
    return static_cast<double>(matrix[0]) * (static_cast<double>(matrix[5]) * matrix[10] -
                                             static_cast<double>(matrix[9]) * matrix[6]) -
           static_cast<double>(matrix[4]) * (static_cast<double>(matrix[1]) * matrix[10] -
                                             static_cast<double>(matrix[9]) * matrix[2]) +
           static_cast<double>(matrix[8]) * (static_cast<double>(matrix[1]) * matrix[6] -
                                             static_cast<double>(matrix[5]) * matrix[2]);
}

std::uint64_t cull_state(schema::CullMode mode, const float* model)
{
    if (mode == schema::CullMode::none)
    {
        return 0;
    }
    const bool mirrored = linear_determinant(model) < 0.0;
    return mirrored != (mode == schema::CullMode::front) ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
}

bool camera_view(const float* world, float* view)
{
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
    if (!(right_length > 0.0))
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

std::array<float, 3> transform_point(const float* matrix,
                                     const std::array<float, 3>& point) noexcept
{
    std::array<float, 3> result{};
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        result[axis] = matrix[axis] * point[0] + matrix[axis + 4] * point[1] +
                       matrix[axis + 8] * point[2] + matrix[axis + 12];
    }
    return result;
}

bool normal_matrix(const float* model, float* normal)
{
    const double determinant = linear_determinant(model);
    if (determinant == 0.0)
    {
        return false;
    }
    // 直接计算逆转置的 3x3 部分；double 避免较大缩放时 float 行列式溢出。
    const double a = model[0], b = model[4], c = model[8];
    const double d = model[1], e = model[5], f = model[9];
    const double g = model[2], h = model[6], i = model[10];
    const double cofactors[] = {e * i - f * h, c * h - b * i, b * f - c * e,
                                f * g - d * i, a * i - c * g, c * d - a * f,
                                d * h - e * g, b * g - a * h, a * e - b * d};
    // 全矩阵统一缩放不改变法线方向，避免 Shader normalize 的长度平方溢出。
    double largest = 0.0;
    for (double value : cofactors)
    {
        largest = std::max(largest, std::abs(value));
    }
    if (!(largest > 0.0))
    {
        return false;
    }
    const double divisor = determinant < 0.0 ? -largest : largest;
    std::fill(normal, normal + 16, 0.0F);
    for (std::size_t column = 0; column < 3; ++column)
    {
        for (std::size_t row = 0; row < 3; ++row)
        {
            normal[column * 4 + row] = static_cast<float>(cofactors[column * 3 + row] / divisor);
        }
    }
    normal[15] = 1.0F;
    return finite_matrix(normal);
}

} // namespace engine::render_detail
