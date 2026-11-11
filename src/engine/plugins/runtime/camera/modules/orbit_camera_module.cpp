#include "plugins/runtime/camera/modules/orbit_camera_module.hpp"
#include "plugins/runtime/camera/camera_math.hpp"
namespace engine::plugins::camera
{
bool OrbitCameraModule::validate(const Data& d, const schema::CameraControllerDataT& def,
                                 std::string& e)
{
    return (detail::slot(def, d.follow_slot) &&
            (!d.height_offset || detail::valid_input(d.height_offset.get(), def)) &&
            detail::finite(d.pivot_offset) && detail::nonnegative(d.position_damping) &&
            detail::bounded(d.rotation_damping, 0, 1000) &&
            detail::bounded(d.pitch_min, -89.9, 89.9) &&
            detail::bounded(d.pitch_max, d.pitch_min, 89.9) &&
            detail::valid_input(d.distance.get(), def) && detail::valid_input(d.yaw.get(), def) &&
            detail::valid_input(d.pitch.get(), def) &&
            detail::valid_input(d.shoulder_offset.get(), def)) ||
           detail::fail(e, "orbit: slot, inputs, pitch range or damping");
}
bool OrbitCameraModule::evaluate(const Data& d, const Runtime& current, Runtime& next,
                                 const CameraEvaluationContext& c, CameraPose& p)
{
    CameraTargetPose t;
    float distance, yaw, pitch, shoulder, height = 0;
    if ((d.height_offset && !detail::input(d.height_offset.get(), c, height)) ||
        !c.target(d.follow_slot, t) || !detail::input(d.distance.get(), c, distance) ||
        !detail::input(d.yaw.get(), c, yaw) || !detail::input(d.pitch.get(), c, pitch) ||
        !detail::input(d.shoulder_offset.get(), c, shoulder) || distance < 0)
    {
        return false;
    }
    const auto q = detail::rotation({std::clamp(pitch, d.pitch_min, d.pitch_max), yaw, 0});
    const auto desired =
        detail::add(detail::add(detail::add(t.position, d.pivot_offset), Vec3(0, height, 0)),
                    detail::rotate(q, {shoulder, 0, distance}));
    p.position =
        c.cut ? desired
              : detail::damp(current.pose.position, desired, d.position_damping, c.delta_time);
    p.rotation_xyzw = c.cut ? q
                            : detail::slerp(current.pose.rotation_xyzw, q,
                                            detail::damping(c.delta_time, d.rotation_damping));
    next.pose = p;
    return true;
}
} // namespace engine::plugins::camera
