#include "plugins/runtime/camera/modules/fixed_camera_module.hpp"
#include "plugins/runtime/camera/camera_math.hpp"
namespace engine::plugins::camera
{
bool FixedCameraModule::validate(const Data& d, const schema::CameraControllerDataT& def,
                                 std::string& e)
{
    return (detail::finite(d.position) && detail::finite(d.rotation_degrees) &&
            detail::slot(def, d.anchor_slot, true)) ||
           detail::fail(e, "fixed: position, rotation or anchor_slot");
}
bool FixedCameraModule::evaluate(const Data& d, const Runtime&, Runtime& next,
                                 const CameraEvaluationContext& c, CameraPose& p)
{
    p.position = d.position;
    p.rotation_xyzw = detail::rotation(d.rotation_degrees);
    if (!d.anchor_slot.empty())
    {
        CameraTargetPose t;
        if (!c.target(d.anchor_slot, t))
        {
            return false;
        }
        p.position = detail::add(t.position, detail::rotate(t.rotation_xyzw, p.position));
        p.rotation_xyzw = detail::product(t.rotation_xyzw, p.rotation_xyzw);
    }
    next.pose = p;
    return true;
}
} // namespace engine::plugins::camera
