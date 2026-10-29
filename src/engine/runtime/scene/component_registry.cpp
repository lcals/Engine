#include "runtime/scene/component_registry.hpp"

#include "runtime/scene/audio_listener.hpp"
#include "runtime/scene/audio_source.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/transform.hpp"

namespace engine
{

Component* ComponentRegistry::create(ComponentType type, Node& node) const
{
    const auto entry = factories_.find(type);
    return entry == factories_.end() ? nullptr : entry->second(node);
}

bool register_scene_components(ComponentRegistry& registry)
{
    return registry.add<Transform>() && registry.add<Camera>() && registry.add<Light>() &&
           registry.add<MeshRenderer>() && registry.add<Collider>() && registry.add<RigidBody>() &&
           registry.add<AudioSource>() && registry.add<AudioListener>();
}

} // namespace engine
