#pragma once

#include "runtime/scene/component.hpp"

#include <map>

struct ecs_world_t;

namespace engine
{
struct ComponentState
{
    schema::ComponentFlagsT flags;
    std::uint64_t instance_id = 0;
    std::uint64_t revision = 1;
    bool transient = false;
};

class ComponentRegistry final
{
public:
    // Per-type operations; storage and callbacks belong to the registering module.
    struct Operations
    {
        Component (*create)(Node) = nullptr;
        bool (*remove)(Node) = nullptr;
        ComponentState* (*state)(Node) = nullptr;
        void* (*data)(Node) = nullptr;
        void* (*runtime)(Node) = nullptr;
        bool (*snapshot)(Component, schema::ComponentDataT&) = nullptr;
        bool (*build)(Component, schema::ComponentDataT&) = nullptr;
        bool (*restore)(Component, const schema::ComponentDataT&) = nullptr;
        bool (*remap)(Component, schema::ComponentDataT&,
                      std::span<const schema::NodeIdMappingT>) = nullptr;
    };

    template <class T> [[nodiscard]] bool add()
    {
        return T::register_type(*this);
    }
    [[nodiscard]] bool contains(ComponentType type) const noexcept;
    [[nodiscard]] Component create(ComponentType type, Node node) const;
    [[nodiscard]] Component find(ComponentType type, Node node) const noexcept;
    [[nodiscard]] bool remove(ComponentType type, Node node) const;
    [[nodiscard]] bool collect(Node node, std::vector<Component>& output) const;
    [[nodiscard]] const Operations* operations(ComponentType type) const noexcept;
    [[nodiscard]] bool register_type(ComponentType type, const Operations& operations);
    [[nodiscard]] ecs_world_t* native_world() const noexcept;

private:
    friend class WorldSystem;
    ecs_world_t* world_ = nullptr;
    std::map<ComponentType, Operations> operations_;
};
[[nodiscard]] std::uint64_t next_component_instance_id() noexcept;
[[nodiscard]] bool register_scene_components(ComponentRegistry& registry);
} // namespace engine
