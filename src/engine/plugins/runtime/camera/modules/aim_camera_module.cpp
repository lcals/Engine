#include "plugins/runtime/camera/modules/aim_camera_module.hpp"
#include "plugins/runtime/camera/camera_math.hpp"
namespace engine::plugins::camera
{
bool AimCameraModule::validate(const Data& d, const schema::CameraControllerDataT& def,
                               std::string& e)
{
    const auto valid_zone = [](Vec2 v)
    { return detail::bounded(v.x(), 0, 1) && detail::bounded(v.y(), 0, 1); };
    return (detail::slot(def, d.target_slot) && detail::finite(d.target_offset) &&
            detail::finite(d.world_up) && detail::length(d.world_up) > 1e-6 &&
            valid_zone(d.screen_position) && valid_zone(d.dead_zone) && valid_zone(d.soft_zone) &&
            d.dead_zone.x() <= d.soft_zone.x() && d.dead_zone.y() <= d.soft_zone.y() &&
            detail::bounded(d.rotation_damping, 0, 1000) &&
            detail::bounded(d.aspect_ratio, 0.01, 100)) ||
           detail::fail(e, "aim: target, composition zones, up or damping");
}
bool AimCameraModule::evaluate(const Data& d, const Runtime& current, Runtime& next,
                               const CameraEvaluationContext& c, CameraPose& p)
{
    using namespace detail;
    CameraTargetPose t;
    if (!c.target(d.target_slot, t))
    {
        return false;
    }
    const auto forward = unit(sub(add(t.position, d.target_offset), p.position));
    const auto right = unit(cross(forward, d.world_up));
    const auto up = cross(right, forward);
    if (length(right) < 1e-6 || length(forward) < 1e-6)
    {
        return false;
    }
    float matrix[16] = {float(right.x()),
                        float(right.y()),
                        float(right.z()),
                        0,
                        float(up.x()),
                        float(up.y()),
                        float(up.z()),
                        0,
                        float(-forward.x()),
                        float(-forward.y()),
                        float(-forward.z()),
                        0,
                        0,
                        0,
                        0,
                        1};
    auto desired = from_matrix(matrix);
    const double vertical =
        p.lens.projection == schema::Projection::perspective
            ? std::tan(p.lens.fov_degrees * std::numbers::pi / 360)
            : p.lens.orthographic_size / std::max(0.001, length(sub(t.position, p.position)));
    desired = product(
        desired,
        rotation({-std::atan((0.5 - d.screen_position.y()) * 2 * vertical) * 180 / std::numbers::pi,
                  std::atan((d.screen_position.x() - 0.5) * 2 * vertical * d.aspect_ratio) * 180 /
                      std::numbers::pi,
                  0}));
    Q inv = {-current.pose.rotation_xyzw[0], -current.pose.rotation_xyzw[1],
             -current.pose.rotation_xyzw[2], current.pose.rotation_xyzw[3]};
    const auto local = rotate(inv, forward);
    const double depth = std::max(1e-6, -local.z());
    const double sx = 0.5 + local.x() / (depth * 2 * vertical * d.aspect_ratio),
                 sy = 0.5 - local.y() / (depth * 2 * vertical);
    const double dx = std::abs(sx - d.screen_position.x()),
                 dy = std::abs(sy - d.screen_position.y());
    if (!c.cut && local.z() < 0 && dx <= d.dead_zone.x() * 0.5 && dy <= d.dead_zone.y() * 0.5)
    {
        p.rotation_xyzw = current.pose.rotation_xyzw;
    }
    else
    {
        const bool outside =
            local.z() >= 0 || dx > d.soft_zone.x() * 0.5 || dy > d.soft_zone.y() * 0.5;
        p.rotation_xyzw = c.cut || outside ? desired
                                           : slerp(current.pose.rotation_xyzw, desired,
                                                   damping(c.delta_time, d.rotation_damping));
    }
    next.pose = p;
    return true;
}
} // namespace engine::plugins::camera
