#include "runtime/render/render_math.hpp"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace engine::render_detail
{

bool projection_camera_settings(const float* projection, float aspect, bool homogeneous,
                                CameraSettings& output)
{
    output = {};
    if (!projection || !finite_matrix(projection) || !std::isfinite(aspect) || aspect <= 0)
    {
        return false;
    }
    const bool orthographic = std::abs(projection[11]) < 0.000001F && projection[15] > 0;
    const bool perspective = projection[11] < 0 && std::abs(projection[15]) < 0.000001F;
    if (!orthographic && !perspective)
    {
        return false;
    }
    float inverse[16];
    bx::mtxInverse(inverse, projection);
    if (!finite_matrix(inverse))
    {
        return false;
    }
    double near_clip = std::numeric_limits<double>::max();
    double far_clip = 0, half_height = 0, slope = 0;
    for (unsigned corner = 0; corner < 8; ++corner)
    {
        const std::array<double, 4> clip{(corner & 1) ? 1.0 : -1.0, (corner & 2) ? 1.0 : -1.0,
                                         (corner & 4)  ? 1.0
                                         : homogeneous ? -1.0
                                                       : 0.0,
                                         1.0};
        std::array<double, 4> point{};
        for (std::size_t row = 0; row < 4; ++row)
        {
            for (std::size_t column = 0; column < 4; ++column)
            {
                point[row] += inverse[column * 4 + row] * clip[column];
            }
        }
        if (!(point[3] > 0))
        {
            return false;
        }
        const double depth = -point[2] / point[3];
        const double extent = std::max(std::abs(point[0] / aspect), std::abs(point[1])) / point[3];
        if (!std::isfinite(depth) || !std::isfinite(extent) || depth <= 0)
        {
            return false;
        }
        near_clip = std::min(near_clip, depth);
        far_clip = std::max(far_clip, depth);
        half_height = std::max(half_height, extent);
        slope = std::max(slope, extent / depth);
    }
    if (!(far_clip > near_clip) || far_clip > std::numeric_limits<float>::max() ||
        half_height > std::numeric_limits<float>::max())
    {
        return false;
    }
    output.projection =
        orthographic ? schema::Projection::orthographic : schema::Projection::perspective;
    output.near_clip = static_cast<float>(near_clip);
    output.far_clip = static_cast<float>(far_clip);
    output.orthographic_size = static_cast<float>(half_height);
    output.fov_degrees = static_cast<float>(std::atan(slope) * 360.0 / std::numbers::pi);
    return output.near_clip > 0 && output.far_clip > output.near_clip && output.fov_degrees > 0 &&
           output.fov_degrees < 180;
}

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
