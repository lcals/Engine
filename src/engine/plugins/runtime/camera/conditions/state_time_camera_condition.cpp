#include "plugins/runtime/camera/conditions/state_time_camera_condition.hpp"
#include "plugins/runtime/camera/conditions/comparison.hpp"
namespace engine::plugins::camera
{
bool StateTimeCameraCondition::validate(const Data& d, const schema::CameraControllerDataT&,
                                        std::string& e)
{
    return (detail::valid_comparison(d.comparison) && detail::bounded(d.seconds, 0, 1e12)) ||
           detail::fail(e, "state_time condition: comparison or seconds");
}
bool StateTimeCameraCondition::evaluate(const Data& d, const CameraEvaluationContext& c,
                                        CameraConditionMatch& out)
{
    out = {};
    out.matched = detail::comparison(d.comparison, c.state_time, d.seconds);
    return true;
}
} // namespace engine::plugins::camera
