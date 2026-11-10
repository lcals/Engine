#include "runtime/render/scene_render_query.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"

#include "runtime/scene/camera.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/text_component.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>

namespace engine::render_detail
{

Node scope_node(const Scene& scene, std::uint64_t id)
{
    if (auto node = scene.find_node(id))
    {
        return node;
    }
    if (&scene == scene.world().scene())
    {
        return {};
    }
    bool append = false;
    auto query = flecs::world(scene.world().native_world()).query<const ecs_detail::ScopeState>();
    query.each(
        [&](const ecs_detail::ScopeState& state)
        {
            if (state.scope == scene.id())
            {
                append = state.destination != 0;
            }
        });
    return append ? scene.world().find_node(id) : Node{};
}
std::uint64_t target_scope(const Scene& scene)
{
    if (&scene != scene.world().scene())
    {
        return scene.id();
    }
    std::uint64_t id = scene.id(), revision = 0;
    auto query = flecs::world(scene.world().native_world()).query<const ecs_detail::ScopeState>();
    query.each(
        [&](flecs::entity entity, const ecs_detail::ScopeState& state)
        {
            if (state.published && entity.has<ecs_detail::Published>() &&
                state.publication_revision > revision &&
                scene.world().scope_candidate({scene.world().epoch(), state.scope}) == &scene)
            {
                id = state.scope;
                revision = state.publication_revision;
            }
        });
    return id;
}

bool drawable_scene(const WorldSystem& world, Camera camera, bool has_extensions,
                    const Scene* candidate)
{
    const auto* scene = candidate ? candidate : world.scene();
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
    auto environments =
        flecs::world(world.native_world())
            .query_builder<const ecs_detail::ComponentStorage<EnvironmentComponent::Data>,
                           const ecs_detail::NodeRuntime>()
            .build();
    environments.each(
        [&](flecs::entity entity, const auto& stored, const ecs_detail::NodeRuntime& state)
        {
            if (candidate ? !scope_node(*candidate, entity.id())
                          : !entity.has<ecs_detail::Published>())
            {
                return;
            }
            drawable = drawable || (state.scene == scene && state.active &&
                                    state.visible_in_hierarchy && stored.state.flags.enabled &&
                                    stored.state.flags.visible && stored.data.atmosphere_enabled);
        });
    if (drawable)
    {
        return true;
    }
    auto meshes = flecs::world(world.native_world())
                      .query_builder<const ecs_detail::ComponentStorage<MeshRenderer::Data>,
                                     const ecs_detail::NodeRuntime>()
                      .build();
    meshes.each(
        [&](flecs::entity entity, const auto& stored, const ecs_detail::NodeRuntime& state)
        {
            if (candidate ? !scope_node(*candidate, entity.id())
                          : !entity.has<ecs_detail::Published>())
            {
                return;
            }
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
                     .build();
    texts.each(
        [&](flecs::entity entity, const auto& stored, const ecs_detail::NodeRuntime& state)
        {
            if (candidate ? !scope_node(*candidate, entity.id())
                          : !entity.has<ecs_detail::Published>())
            {
                return;
            }
            drawable = drawable || (state.active && state.visible_in_hierarchy &&
                                    stored.state.flags.enabled && stored.state.flags.visible);
        });
    return drawable;
}

} // namespace engine::render_detail
