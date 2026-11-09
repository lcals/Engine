#pragma once
#include "plugins/runtime/camera/camera_type_registry.hpp"
namespace engine::plugins::camera
{
class CameraStateMachine
{
public:
    [[nodiscard]] bool select(const schema::CameraStateMachineDataT&, const CameraMachineRuntime&,
                              const CameraEvaluationContext&, const CameraTypeRegistry&,
                              CameraTransitionRequest&) const;
    void commit(const CameraTransitionRequest&, CameraMachineRuntime&) const;
};
} // namespace engine::plugins::camera
