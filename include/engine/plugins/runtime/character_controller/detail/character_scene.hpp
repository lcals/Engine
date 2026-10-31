#pragma once

#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"

namespace engine::plugins::character_controller::detail
{

[[nodiscard]] inline bool active(const Node& node) noexcept
{
    for (const Node* current = &node; current != nullptr; current = current->parent())
    {
        const auto* transform = current->get_component<Transform>();
        if (transform == nullptr || !transform->enabled())
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline Node* find_local_node(Scene& scene, std::uint64_t id) noexcept
{
    for (const auto& node : scene.nodes())
    {
        if (node->info().id == id)
        {
            return node.get();
        }
    }
    return nullptr;
}

} // namespace engine::plugins::character_controller::detail
