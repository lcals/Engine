#include "runtime/scene/component_registry.hpp"

#include "runtime/scene/animator.hpp"
#include "runtime/scene/audio_listener.hpp"
#include "runtime/scene/audio_source.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/post_process_component.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/text_component.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/scene/ui_component.hpp"

namespace engine
{

bool ComponentRegistry::contains(ComponentType type) const noexcept
{
    return factories_.contains(type);
}

Component* ComponentRegistry::create(ComponentType type, Node& node) const
{
    const auto entry = factories_.find(type);
    return entry == factories_.end() ? nullptr : entry->second(node);
}

bool register_scene_components(ComponentRegistry& registry)
{
    return registry.add<Transform>() && registry.add<Camera>() && registry.add<Light>() &&
           registry.add<MeshRenderer>() && registry.add<Collider>() && registry.add<RigidBody>() &&
           registry.add<AudioSource>() && registry.add<AudioListener>() &&
           registry.add<Animator>() && registry.add<CharacterMotor>() &&
           registry.add<UiComponent>() && registry.add<TextComponent>() &&
           registry.add<EnvironmentComponent>() && registry.add<PostProcessComponent>();
}

} // namespace engine
