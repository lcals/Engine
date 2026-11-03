#pragma once

#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"

namespace engine::plugins::character_controller::detail
{

[[nodiscard]] inline bool active(const Node& node) noexcept
{
    for (Node current = node; static_cast<bool>(current); current = current.parent())
    {
        const auto transform = current.get_component<Transform>();
        if (!transform || !transform.enabled())
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline Node find_local_node(const Scene& scene, std::uint64_t id) noexcept
{
    Node result;
    auto& world = scene.world();
    auto query = flecs::world(world.native_world())
                     .query_builder<const schema::NodePropertiesT, const ecs_detail::NodeRuntime>()
                     .with<ecs_detail::Published>()
                     .build();
    query.each(
        [&](flecs::entity entity, const schema::NodePropertiesT& properties,
            const ecs_detail::NodeRuntime& state)
        {
            if (state.scene == &scene && properties.id == id)
            {
                result = world.find_node(entity.id());
            }
        });
    return result;
}

} // namespace engine::plugins::character_controller::detail
