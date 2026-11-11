#pragma once
#include "plugins/runtime/camera/camera_types.hpp"
namespace engine::plugins::camera
{
class StateTimeCameraCondition
{
public:
    using Data = schema::CameraTimeTestDataT;
    static constexpr std::string_view type()
    {
        return "state_time";
    }
    static const Data* find(const schema::CameraControllerDataT& d, std::uint64_t id)
    {
        if (d.machine && d.machine->condition_data)
        {
            for (const auto& v : d.machine->condition_data->time_tests)
            {
                if (v.id == id)
                {
                    return &v;
                }
            }
        }
        return nullptr;
    }
    static const Data* find(const schema::CameraStateMachineDataT& d, std::uint64_t id)
    {
        if (d.condition_data)
        {
            for (const auto& v : d.condition_data->time_tests)
            {
                if (v.id == id)
                {
                    return &v;
                }
            }
        }
        return nullptr;
    }
    [[nodiscard]] static bool validate(const Data&, const schema::CameraControllerDataT&,
                                       std::string&);
    [[nodiscard]] static bool evaluate(const Data&, const CameraEvaluationContext&,
                                       CameraConditionMatch&);
};
} // namespace engine::plugins::camera
