#pragma once
#include "plugins/runtime/camera/camera_controller.hpp"
#include "runtime/schema/control_json_generated.hpp"
namespace engine::plugins::camera::detail
{
inline nlohmann::json status_json(const CameraStatus& s)
{
    static constexpr const char* states[] = {"inactive", "waiting", "ready",
                                             "blending", "blocked", "error"};
    return {{"runState", states[static_cast<int>(s.run_state)]},
            {"hasOutput", s.has_output},
            {"state", s.state},
            {"sourceState", s.source_state},
            {"rig", s.rig},
            {"stateTime", s.state_time},
            {"blendProgress", s.blend_progress},
            {"diagnostic", s.diagnostic},
            {"output",
             {{"position", schema::control_json::write(s.output.position, true)},
              {"rotation", s.output.rotation_xyzw},
              {"lens", schema::control_json::write(s.output.lens, true)}}}};
}
inline bool read_parameters(CameraController solver, const nlohmann::json& j,
                            std::vector<schema::CameraParameterValueT>& values)
{
    values.clear();
    if (!j.is_object())
    {
        return false;
    }
    for (auto i = j.begin(); i != j.end(); ++i)
    {
        schema::CameraParameterValueT p;
        std::string error;
        if (!solver.get_parameter(i.key(), p))
        {
            return false;
        }
        switch (p.type)
        {
        case schema::CameraParameterType::float_value:
            if (!schema::control_json::read(i.value(), p.float_value, error, false, i.key()))
            {
                return false;
            }
            break;
        case schema::CameraParameterType::int_value:
            if (!schema::control_json::read(i.value(), p.int_value, error, false, i.key()))
            {
                return false;
            }
            break;
        case schema::CameraParameterType::bool_value:
            if (!schema::control_json::read(i.value(), p.bool_value, error, false, i.key()))
            {
                return false;
            }
            break;
        default:
            return false;
        }
        values.push_back(p);
    }
    return true;
}
} // namespace engine::plugins::camera::detail
