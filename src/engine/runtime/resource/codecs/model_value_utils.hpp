#pragma once

#include "runtime/schema/common_generated.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace engine::model_value_utils
{
inline bool number(double value) noexcept
{
    return std::isfinite(value) && std::abs(value) <= 1.0e12;
}
inline bool vector(const schema::Vec3& value) noexcept
{
    return number(value.x()) && number(value.y()) && number(value.z());
}
inline bool scale(const schema::Vec3& value) noexcept
{
    return vector(value) && std::abs(value.x()) > 1.0e-8 && std::abs(value.y()) > 1.0e-8 &&
           std::abs(value.z()) > 1.0e-8;
}
inline bool quaternion(const schema::Vec4& value) noexcept
{
    if (!number(value.x()) || !number(value.y()) || !number(value.z()) || !number(value.w()))
    {
        return false;
    }
    const double length = value.x() * value.x() + value.y() * value.y() + value.z() * value.z() +
                          value.w() * value.w();
    return std::abs(length - 1.0) < 0.001;
}
inline bool affine(std::span<const float> value) noexcept
{
    if (value.size() != 16 || !std::all_of(value.begin(), value.end(), number) ||
        std::abs(value[3]) > 1.0e-6F || std::abs(value[7]) > 1.0e-6F ||
        std::abs(value[11]) > 1.0e-6F || std::abs(value[15] - 1.0F) > 1.0e-6F)
    {
        return false;
    }
    const double determinant =
        static_cast<double>(value[0]) *
            (static_cast<double>(value[5]) * value[10] - static_cast<double>(value[9]) * value[6]) -
        static_cast<double>(value[4]) *
            (static_cast<double>(value[1]) * value[10] - static_cast<double>(value[9]) * value[2]) +
        static_cast<double>(value[8]) *
            (static_cast<double>(value[1]) * value[6] - static_cast<double>(value[5]) * value[2]);
    return std::isfinite(determinant) && std::abs(determinant) > 1.0e-12;
}
} // namespace engine::model_value_utils
