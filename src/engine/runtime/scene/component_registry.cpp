#include "runtime/scene/component_registry.hpp"

#include "runtime/scene/animator.hpp"
#include "runtime/scene/audio_listener.hpp"
#include "runtime/scene/audio_source.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/light_probe_volume_component.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/planar_reflection_component.hpp"
#include "runtime/scene/post_process_component.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/text_component.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/scene/ui_component.hpp"

#include "runtime/world/world_system.hpp"

namespace engine
{
bool ComponentRegistry::contains(ComponentType type) const noexcept
{
    return operations_.contains(type);
}
const ComponentRegistry::Operations*
ComponentRegistry::operations(ComponentType type) const noexcept
{
    const auto found = operations_.find(type);
    return found == operations_.end() ? nullptr : &found->second;
}
ecs_world_t* ComponentRegistry::native_world() const noexcept
{
    return world_;
}
bool ComponentRegistry::register_type(ComponentType type, const Operations& operations)
{
    if (!world_ || type == ComponentType::NONE || !operations.create || !operations.remove ||
        !operations.state || !operations.data || !operations.runtime || !operations.snapshot ||
        !operations.build || !operations.restore || !operations.remap)
    {
        return false;
    }
    return operations_.emplace(type, operations).second;
}
Component ComponentRegistry::create(ComponentType type, Node node) const
{
    const auto* ops = operations(type);
    return ops && node && node.world()->native_world() == world_ ? ops->create(node) : Component{};
}
Component ComponentRegistry::find(ComponentType type, Node node) const noexcept
{
    const auto* ops = operations(type);
    if (!ops || !node || node.world()->native_world() != world_)
    {
        return {};
    }
    const auto* state = ops->state(node);
    return state && state->instance_id != 0 ? Component(node, type, state->instance_id)
                                            : Component{};
}
bool ComponentRegistry::remove(ComponentType type, Node node) const
{
    const auto* ops = operations(type);
    return ops && node && node.world()->native_world() == world_ && ops->remove(node);
}
bool ComponentRegistry::collect(Node node, std::vector<Component>& output) const
{
    output.clear();
    if (!node || node.world()->native_world() != world_)
    {
        return false;
    }
    for (const auto& [type, ops] : operations_)
    {
        const auto* state = ops.state(node);
        if (state && state->instance_id != 0)
        {
            output.push_back(Component(node, type, state->instance_id));
        }
    }
    return true;
}
bool register_scene_components(ComponentRegistry& registry)
{
    return registry.add<Transform>() && registry.add<Camera>() && registry.add<Light>() &&
           registry.add<MeshRenderer>() && registry.add<Collider>() && registry.add<RigidBody>() &&
           registry.add<AudioSource>() && registry.add<AudioListener>() &&
           registry.add<Animator>() && registry.add<CharacterMotor>() &&
           registry.add<UiComponent>() && registry.add<TextComponent>() &&
           registry.add<EnvironmentComponent>() && registry.add<PostProcessComponent>() &&
           registry.add<PlanarReflectionComponent>() && registry.add<LightProbeVolumeComponent>();
}
} // namespace engine
