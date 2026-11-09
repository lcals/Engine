#include "plugins/runtime/camera/modules/follow_camera_module.hpp"
#include "plugins/runtime/camera/camera_math.hpp"
namespace engine::plugins::camera
{
bool FollowCameraModule::validate(const Data& d, const schema::CameraControllerDataT& def,
                                  std::string& e)
{
    return (detail::slot(def, d.follow_slot) && detail::finite(d.offset) &&
            detail::nonnegative(d.position_damping) &&
            detail::bounded(d.teleport_distance, 0, 1e9)) ||
           detail::fail(e, "follow: slot, offset, damping or teleport_distance");
}
bool FollowCameraModule::evaluate(const Data& d, const Runtime& current, Runtime& next,
                                  const CameraEvaluationContext& c, CameraPose& p)
{
    CameraTargetPose t;
    if (!c.target(d.follow_slot, t))
    {
        return false;
    }
    const auto desired = detail::add(
        t.position, d.local_offset ? detail::rotate(t.rotation_xyzw, d.offset) : d.offset);
    p.position =
        c.cut || detail::length(detail::sub(desired, current.pose.position)) > d.teleport_distance
            ? desired
            : detail::damp(current.pose.position, desired, d.position_damping, c.delta_time);
    p.rotation_xyzw = t.rotation_xyzw;
    next.pose = p;
    return true;
}
} // namespace engine::plugins::camera
