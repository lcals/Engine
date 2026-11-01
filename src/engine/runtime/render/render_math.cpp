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
