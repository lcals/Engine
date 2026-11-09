#include "plugins/runtime/camera/camera_type_registry.hpp"
#include "plugins/runtime/camera/camera_runtime.hpp"
#include "plugins/runtime/camera/conditions/parameter_camera_condition.hpp"
#include "plugins/runtime/camera/conditions/state_time_camera_condition.hpp"
#include "plugins/runtime/camera/conditions/target_camera_condition.hpp"
#include "plugins/runtime/camera/conditions/trigger_camera_condition.hpp"
#include "plugins/runtime/camera/modules/aim_camera_module.hpp"
#include "plugins/runtime/camera/modules/bounds_camera_module.hpp"
#include "plugins/runtime/camera/modules/collision_camera_module.hpp"
#include "plugins/runtime/camera/modules/fixed_camera_module.hpp"
#include "plugins/runtime/camera/modules/follow_camera_module.hpp"
#include "plugins/runtime/camera/modules/lens_camera_module.hpp"
#include "plugins/runtime/camera/modules/noise_camera_module.hpp"
#include "plugins/runtime/camera/modules/orbit_camera_module.hpp"
namespace engine::plugins::camera
{
const CameraTypeRegistry::ModuleOperations*
CameraTypeRegistry::module(std::string_view n) const noexcept
{
    for (const auto& v : modules_)
    {
        if (v.type == n)
        {
            return &v;
        }
    }
    return nullptr;
}
const CameraTypeRegistry::ConditionOperations*
CameraTypeRegistry::condition(std::string_view n) const noexcept
{
    for (const auto& v : conditions_)
    {
        if (v.type == n)
        {
            return &v;
        }
    }
    return nullptr;
}
const CameraTypeRegistry* camera_types(WorldSystem& world)
{
    const auto* r = flecs::world(world.native_world()).try_get<CameraWorldRuntime>();
    return r ? r->types : nullptr;
}
bool register_camera_components(WorldSystem& world, const CameraTypeRegistry& types)
{
    if (!types.frozen() || !world.native_world())
    {
        return false;
    }
    flecs::world ecs(world.native_world());
    if (const auto* current = ecs.try_get<CameraWorldRuntime>())
    {
        return current->types == &types;
    }
    if (!world.component_registry().contains(CameraController::component_type) &&
        !world.component_registry().add<CameraController>())
    {
        return false;
    }
    for (const auto& ops : types.modules())
    {
        ops.bind(world);
    }
    ecs.set<CameraWorldRuntime>({&types});
    return true;
}
bool CameraEvaluationContext::parameter(std::string_view n,
                                        schema::CameraParameterValueT& out) const
{
    out = {};
    for (const auto& p : parameters)
    {
        if (p.name == n)
        {
            out = p;
            return true;
        }
    }
    return false;
}
bool CameraEvaluationContext::target(std::string_view slot, CameraTargetPose& out) const
{
    out = {};
    for (std::size_t i = 0; i < target_slots.size() && i < targets.size(); ++i)
    {
        if (target_slots[i].name == slot && targets[i].node)
        {
            out = targets[i];
            return true;
        }
    }
    return false;
}
bool register_builtin_camera_types(CameraTypeRegistry& types)
{
    return types.add_module<FixedCameraModule>() && types.add_module<FollowCameraModule>() &&
           types.add_module<OrbitCameraModule>() && types.add_module<AimCameraModule>() &&
           types.add_module<LensCameraModule>() && types.add_module<NoiseCameraModule>() &&
           types.add_module<CollisionCameraModule>() && types.add_module<BoundsCameraModule>() &&
           types.add_condition<ParameterCameraCondition>() &&
           types.add_condition<TriggerCameraCondition>() &&
           types.add_condition<TargetCameraCondition>() &&
           types.add_condition<StateTimeCameraCondition>();
}
} // namespace engine::plugins::camera
