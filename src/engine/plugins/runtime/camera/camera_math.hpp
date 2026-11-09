#pragma once
#include "plugins/runtime/camera/camera_types.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace engine::plugins::camera::detail
{
using Q = std::array<float, 4>;
inline Vec3 add(Vec3 a, Vec3 b)
{
    return {a.x() + b.x(), a.y() + b.y(), a.z() + b.z()};
}
inline Vec3 sub(Vec3 a, Vec3 b)
{
    return {a.x() - b.x(), a.y() - b.y(), a.z() - b.z()};
}
inline Vec3 mul(Vec3 a, double n)
{
    return {a.x() * n, a.y() * n, a.z() * n};
}
inline double dot(Vec3 a, Vec3 b)
{
    return a.x() * b.x() + a.y() * b.y() + a.z() * b.z();
}
inline double length(Vec3 a)
{
    return std::sqrt(dot(a, a));
}
inline Vec3 unit(Vec3 a)
{
    const double n = length(a);
    return n > 1e-10 ? mul(a, 1 / n) : Vec3{};
}
inline Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y() * b.z() - a.z() * b.y(), a.z() * b.x() - a.x() * b.z(),
            a.x() * b.y() - a.y() * b.x()};
}
inline bool finite(Vec3 v)
{
    return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z());
}
inline bool nonnegative(Vec3 v)
{
    return finite(v) && v.x() >= 0 && v.y() >= 0 && v.z() >= 0;
}
inline bool bounded(double v, double a, double b)
{
    return std::isfinite(v) && v >= a && v <= b;
}
inline Q product(Q a, Q b)
{
    return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
            a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
            a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
            a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}
inline Q normalized(Q q)
{
    double n = 0;
    for (auto v : q)
    {
        n += v * v;
    }
    if (n < 1e-12 || !std::isfinite(n))
    {
        return {0, 0, 0, 1};
    }
    for (auto& v : q)
    {
        v = static_cast<float>(v / std::sqrt(n));
    }
    return q;
}
inline Q rotation(Vec3 e)
{
    const double r = std::numbers::pi / 360;
    const auto axis = [&](double d, int i)
    {
        Q q{0, 0, 0, static_cast<float>(std::cos(d * r))};
        q[i] = static_cast<float>(std::sin(d * r));
        return q;
    };
    return normalized(product(product(axis(e.z(), 2), axis(e.y(), 1)), axis(e.x(), 0)));
}
inline Vec3 euler(Q q)
{
    const double r = 180 / std::numbers::pi;
    return {std::atan2(2 * (q[3] * q[0] + q[1] * q[2]), 1 - 2 * (q[0] * q[0] + q[1] * q[1])) * r,
            std::asin(std::clamp(2.0 * (q[3] * q[1] - q[2] * q[0]), -1.0, 1.0)) * r,
            std::atan2(2 * (q[3] * q[2] + q[0] * q[1]), 1 - 2 * (q[1] * q[1] + q[2] * q[2])) * r};
}
inline Vec3 rotate(Q q, Vec3 v)
{
    const auto u = Vec3(q[0], q[1], q[2]);
    return add(v, add(mul(cross(u, v), 2 * q[3]), mul(cross(u, cross(u, v)), 2)));
}
inline Q from_matrix(const float* m)
{
    const double trace = m[0] + m[5] + m[10];
    Q q{};
    if (trace > 0)
    {
        double s = std::sqrt(trace + 1) * 2;
        q = {float((m[6] - m[9]) / s), float((m[8] - m[2]) / s), float((m[1] - m[4]) / s),
             float(s / 4)};
    }
    else if (m[0] > m[5] && m[0] > m[10])
    {
        double s = std::sqrt(1 + m[0] - m[5] - m[10]) * 2;
        q = {float(s / 4), float((m[4] + m[1]) / s), float((m[8] + m[2]) / s),
             float((m[6] - m[9]) / s)};
    }
    else if (m[5] > m[10])
    {
        double s = std::sqrt(1 + m[5] - m[0] - m[10]) * 2;
        q = {float((m[4] + m[1]) / s), float(s / 4), float((m[9] + m[6]) / s),
             float((m[8] - m[2]) / s)};
    }
    else
    {
        double s = std::sqrt(1 + m[10] - m[0] - m[5]) * 2;
        q = {float((m[8] + m[2]) / s), float((m[9] + m[6]) / s), float(s / 4),
             float((m[1] - m[4]) / s)};
    }
    return normalized(q);
}
inline Q slerp(Q a, Q b, double t)
{
    double d = 0;
    for (int i = 0; i < 4; ++i)
    {
        d += a[i] * b[i];
    }
    if (d < 0)
    {
        for (auto& v : b)
        {
            v = -v;
        }
        d = -d;
    }
    double x = 1 - t, y = t;
    if (d < 0.9995)
    {
        double theta = std::acos(std::clamp(d, 0.0, 1.0));
        x = std::sin((1 - t) * theta) / std::sin(theta);
        y = std::sin(t * theta) / std::sin(theta);
    }
    Q q;
    for (int i = 0; i < 4; ++i)
    {
        q[i] = float(a[i] * x + b[i] * y);
    }
    return normalized(q);
}
inline double damping(double dt, double seconds)
{
    return seconds <= 0 ? 1 : -std::expm1(-dt / seconds);
}
inline Vec3 damp(Vec3 a, Vec3 b, Vec3 seconds, double dt)
{
    return {std::lerp(a.x(), b.x(), damping(dt, seconds.x())),
            std::lerp(a.y(), b.y(), damping(dt, seconds.y())),
            std::lerp(a.z(), b.z(), damping(dt, seconds.z()))};
}
inline const schema::CameraParameterDataT* parameter(const schema::CameraControllerDataT& d,
                                                     std::string_view n)
{
    for (const auto& p : d.parameters)
    {
        if (p.name == n)
        {
            return &p;
        }
    }
    return nullptr;
}
inline bool slot(const schema::CameraControllerDataT& d, std::string_view n, bool optional = false)
{
    if (n.empty())
    {
        return optional;
    }
    for (const auto& t : d.targets)
    {
        if (t.name == n)
        {
            return true;
        }
    }
    return false;
}
inline bool valid_input(const schema::CameraFloatInputDataT* v,
                        const schema::CameraControllerDataT& d)
{
    if (!v || !std::isfinite(v->constant) || !std::isfinite(v->scale) || !std::isfinite(v->offset))
    {
        return false;
    }
    if (v->clamp_enabled &&
        (!std::isfinite(v->minimum) || !std::isfinite(v->maximum) || v->minimum > v->maximum))
    {
        return false;
    }
    if (v->parameter.empty())
    {
        return true;
    }
    const auto* p = parameter(d, v->parameter);
    return p && p->type == schema::CameraParameterType::float_value;
}
inline bool input(const schema::CameraFloatInputDataT* v, const CameraEvaluationContext& c,
                  float& out)
{
    if (!v)
    {
        return false;
    }
    out = v->constant;
    if (!v->parameter.empty())
    {
        schema::CameraParameterValueT p;
        if (!c.parameter(v->parameter, p) || p.type != schema::CameraParameterType::float_value)
        {
            return false;
        }
        out = p.float_value * v->scale + v->offset;
    }
    if (v->clamp_enabled)
    {
        out = std::clamp(out, v->minimum, v->maximum);
    }
    return std::isfinite(out);
}
inline bool valid_lens(const CameraSettings& p)
{
    return p.projection >= schema::Projection::MIN && p.projection <= schema::Projection::MAX &&
           bounded(p.fov_degrees, 0.01, 179) && bounded(p.orthographic_size, 0.0001, 1000000) &&
           bounded(p.near_clip, 0.00001, 1000000) &&
           bounded(p.far_clip, p.near_clip + 0.00001, 1e9);
}
inline bool valid_pose(const CameraPose& p)
{
    double n = 0;
    for (auto v : p.rotation_xyzw)
    {
        if (!std::isfinite(v))
        {
            return false;
        }
        n += v * v;
    }
    return finite(p.position) && std::abs(n - 1) < 0.001 && valid_lens(p.lens);
}
inline bool fail(std::string& error, std::string_view text)
{
    error = text;
    return false;
}
} // namespace engine::plugins::camera::detail
