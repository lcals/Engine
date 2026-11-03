#pragma once

#include "runtime/scene/scene_types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine
{
class Scene;
class WorldSystem;
class Component;

class Node final
{
public:
    Node() noexcept = default;
    [[nodiscard]] bool valid() const noexcept;
    explicit operator bool() const noexcept
    {
        return valid();
    }
    [[nodiscard]] bool operator==(const Node&) const noexcept = default;

    [[nodiscard]] std::uint64_t id() const noexcept;
    [[nodiscard]] std::string name() const;
    [[nodiscard]] schema::NodeInfoT info() const;
    [[nodiscard]] bool build_info(schema::NodeInfoT& output) const;
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] bool set_enabled(bool value) noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] bool set_visible(bool value) noexcept;
    [[nodiscard]] bool visible_in_hierarchy() const noexcept;
    [[nodiscard]] Scene* scene() const noexcept;
    // Borrowed WorldSystem must outlive calls through this handle.
    [[nodiscard]] WorldSystem* world() const noexcept;
    [[nodiscard]] Node parent() const noexcept;
    [[nodiscard]] bool set_parent(Node parent) noexcept;
    [[nodiscard]] bool transient() const noexcept;
    [[nodiscard]] bool set_transient(bool value) noexcept;
    [[nodiscard]] bool persistent() const noexcept;
    [[nodiscard]] bool set_persistent(bool value) noexcept;

    template <class T> [[nodiscard]] T add_component()
    {
        return T(add_component(T::component_type));
    }
    template <class T> [[nodiscard]] T get_component() const noexcept
    {
        return T(get_component(T::component_type));
    }
    [[nodiscard]] Component add_component(ComponentType type);
    [[nodiscard]] Component get_component(ComponentType type) const noexcept;
    [[nodiscard]] bool remove_component(ComponentType type);
    [[nodiscard]] bool collect_components(std::vector<Component>& output) const;

private:
    friend class Scene;
    friend class WorldSystem;
    Node(WorldSystem& world, std::uint64_t epoch, std::uint64_t entity) noexcept;
    WorldSystem* world_ = nullptr;
    std::uint64_t world_epoch_ = 0;
    std::uint64_t entity_id_ = 0;
};
} // namespace engine
