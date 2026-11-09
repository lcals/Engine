#include "plugins/runtime/camera/conditions/trigger_camera_condition.hpp"
#include "plugins/runtime/camera/camera_math.hpp"
namespace engine::plugins::camera
{
bool TriggerCameraCondition::validate(const Data& d, const schema::CameraControllerDataT& def,
                                      std::string& e)
{
    const auto* p = detail::parameter(def, d.parameter);
    return (p && p->type == schema::CameraParameterType::trigger) ||
           detail::fail(e, "trigger condition: unknown trigger");
}
bool TriggerCameraCondition::evaluate(const Data& d, const CameraEvaluationContext& c,
                                      CameraConditionMatch& out)
{
    out = {};
    schema::CameraParameterValueT p;
    if (!c.parameter(d.parameter, p) || p.type != schema::CameraParameterType::trigger)
    {
        return false;
    }
    out.matched = p.bool_value;
    if (out.matched)
    {
        out.read_triggers.push_back(d.parameter);
    }
    return true;
}
} // namespace engine::plugins::camera
