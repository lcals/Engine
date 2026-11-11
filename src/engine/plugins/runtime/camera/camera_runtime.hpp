#pragma once
#include "plugins/runtime/camera/camera_controller.hpp"
#include "plugins/runtime/camera/camera_type_registry.hpp"
#include <map>
namespace engine::plugins::camera
{
struct CameraWorldRuntime
{
    const CameraTypeRegistry* types = nullptr;
};
struct CameraControllerRuntime
{
    std::vector<schema::CameraParameterValueT> parameter_values;
    std::map<std::string, Node, std::less<>> target_overrides;
    std::vector<Node> default_targets;
    std::vector<bool> default_resolved;
    CameraMachineRuntime machine;
    std::map<std::string, std::vector<std::size_t>, std::less<>> outgoing;
    CameraPose committed_base_pose;
    CameraStatus status;
    bool was_active = false, reseed = true;
    std::uint64_t definition_revision = 0;
};
const CameraTypeRegistry* camera_types(WorldSystem& world);
bool valid_parameter_value(const schema::CameraParameterDataT&,
                           const schema::CameraParameterValueT&);
bool valid_camera_target(Node camera, Node target);
} // namespace engine::plugins::camera
