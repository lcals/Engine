#include "plugins/runtime/camera/conditions/target_camera_condition.hpp"
#include "plugins/runtime/camera/camera_math.hpp"
namespace engine::plugins::camera
{
bool TargetCameraCondition::validate(const Data& d, const schema::CameraControllerDataT& def,
                                     std::string& e)
{
    return detail::slot(def, d.slot) || detail::fail(e, "target condition: unknown slot");
}
bool TargetCameraCondition::evaluate(const Data& d, const CameraEvaluationContext& c,
                                     CameraConditionMatch& out)
{
    out = {};
    CameraTargetPose t;
    out.matched = c.target(d.slot, t) == d.expected_valid;
    return true;
}
} // namespace engine::plugins::camera
