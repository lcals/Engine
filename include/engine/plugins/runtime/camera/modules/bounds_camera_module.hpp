#pragma once
#include "plugins/runtime/camera/camera_types.hpp"
namespace engine::plugins::camera
{
class BoundsCameraModule
{
public:
    using Data = schema::CameraBoundsDataT;
    struct Runtime
    {
        CameraPose pose;
    };
    static constexpr std::string_view type()
    {
        return "bounds";
    }
    static constexpr CameraModuleStage stage()
    {
        return CameraModuleStage::constraint;
    }
    static const Data* find(const schema::CameraControllerDataT& d, std::uint64_t id)
    {
        if (d.module_data)
        {
            for (const auto& v : d.module_data->bounds)
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
    [[nodiscard]] static bool seed(const Data&, const CameraPose& pose, Runtime& output)
    {
        output = {};
        output.pose = pose;
        return true;
    }
    [[nodiscard]] static bool evaluate(const Data&, const Runtime&, Runtime&,
                                       const CameraEvaluationContext&, CameraPose&);
    [[nodiscard]] static bool check(const Data&, const CameraEvaluationContext&, const CameraPose&,
                                    bool&);
};
} // namespace engine::plugins::camera
