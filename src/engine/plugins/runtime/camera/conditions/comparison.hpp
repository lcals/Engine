#pragma once
#include "plugins/runtime/camera/camera_math.hpp"
namespace engine::plugins::camera::detail
{
inline bool comparison(schema::CameraComparison kind, double a, double b, double tolerance = 0)
{
    switch (kind)
    {
    case schema::CameraComparison::equal:
        return std::abs(a - b) <= tolerance;
    case schema::CameraComparison::not_equal:
        return std::abs(a - b) > tolerance;
    case schema::CameraComparison::less:
        return a < b;
    case schema::CameraComparison::less_equal:
        return a <= b;
    case schema::CameraComparison::greater:
        return a > b;
    case schema::CameraComparison::greater_equal:
        return a >= b;
    }
    return false;
}
inline bool valid_comparison(schema::CameraComparison c)
{
    return c >= schema::CameraComparison::MIN && c <= schema::CameraComparison::MAX;
}
} // namespace engine::plugins::camera::detail
