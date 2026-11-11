#include "plugins/runtime/camera/conditions/parameter_camera_condition.hpp"
#include "plugins/runtime/camera/conditions/comparison.hpp"
namespace engine::plugins::camera
{
bool ParameterCameraCondition::validate(const Data& d, const schema::CameraControllerDataT& def,
                                        std::string& e)
{
    const auto* p = detail::parameter(def, d.parameter);
    if (!p || !d.value || p->type != d.value->type ||
        p->type == schema::CameraParameterType::trigger ||
        !detail::valid_comparison(d.comparison) || !detail::bounded(d.tolerance, 0, 1e9))
    {
        return detail::fail(e, "parameter condition: type, comparison or tolerance");
    }
    if ((p->type != schema::CameraParameterType::float_value && d.value->float_value != 0) ||
        (p->type != schema::CameraParameterType::int_value && d.value->int_value != 0) ||
        (p->type != schema::CameraParameterType::bool_value && d.value->bool_value))
    {
        return detail::fail(e, "parameter condition: inactive value fields must be empty");
    }
    if (p->type == schema::CameraParameterType::bool_value &&
        d.comparison != schema::CameraComparison::equal &&
        d.comparison != schema::CameraComparison::not_equal)
    {
        return detail::fail(e, "boolean condition requires equal/not_equal");
    }
    if (p->type == schema::CameraParameterType::float_value &&
        (!std::isfinite(d.value->float_value) ||
         ((d.comparison == schema::CameraComparison::equal ||
           d.comparison == schema::CameraComparison::not_equal) &&
          d.tolerance <= 0)))
    {
        return detail::fail(e, "float equality requires explicit positive tolerance");
    }
    return true;
}
bool ParameterCameraCondition::evaluate(const Data& d, const CameraEvaluationContext& c,
                                        CameraConditionMatch& out)
{
    out = {};
    schema::CameraParameterValueT p;
    if (!d.value || !c.parameter(d.parameter, p) || p.type != d.value->type)
    {
        return false;
    }
    const auto number = [](const auto& v) -> double
    {
        if (v.type == schema::CameraParameterType::float_value)
        {
            return v.float_value;
        }
        if (v.type == schema::CameraParameterType::int_value)
        {
            return v.int_value;
        }
        return v.bool_value ? 1 : 0;
    };
    out.matched = detail::comparison(d.comparison, number(p), number(*d.value), d.tolerance);
    return true;
}
} // namespace engine::plugins::camera
