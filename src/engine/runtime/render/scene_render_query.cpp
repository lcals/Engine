#include "runtime/render/scene_render_query.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"

#include "runtime/scene/camera.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/text_component.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>

namespace engine::render_detail
{

bool drawable_scene(const WorldSystem& world, Camera camera, bool has_extensions)
{
    const auto* scene = world.scene();
    camera = camera ? camera : scene ? scene->active_camera() : Camera{};
    if (!camera || !camera.active() || !camera.visible_in_hierarchy())
    {
        return false;
    }
    if (has_extensions)
    {
        return true;
    }
    bool drawable = false;
    auto meshes = flecs::world(world.native_world())
                      .query_builder<const ecs_detail::ComponentStorage<MeshRenderer::Data>,
                                     const ecs_detail::NodeRuntime>()
                      .with<ecs_detail::Published>()
                      .build();
    meshes.each(
        [&](const auto& stored, const ecs_detail::NodeRuntime& state)
        {
            drawable = drawable || (state.active && state.visible_in_hierarchy &&
                                    stored.state.flags.enabled && stored.state.flags.visible);
        });
    if (drawable)
    {
        return true;
    }
    auto texts = flecs::world(world.native_world())
                     .query_builder<const ecs_detail::ComponentStorage<TextComponent::Data>,
                                    const ecs_detail::NodeRuntime>()
                     .with<ecs_detail::Published>()
                     .build();
    texts.each(
        [&](const auto& stored, const ecs_detail::NodeRuntime& state)
        {
            drawable = drawable || (state.active && state.visible_in_hierarchy &&
                                    stored.state.flags.enabled && stored.state.flags.visible);
        });
    return drawable;
}

} // namespace engine::render_detail
