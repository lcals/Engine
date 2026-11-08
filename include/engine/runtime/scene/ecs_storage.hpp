#pragma once

// Implementation-only ECS access shared by core systems and selected plugins.
#include "runtime/scene/component_registry.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/world/world_system.hpp"

#include <flecs.h>
#include <type_traits>

namespace engine
{
namespace ecs_detail
{
template <class Data> struct ComponentStorage
{
    ComponentState state;
    Data data;
};
} // namespace ecs_detail

template <class T, class Data, class Runtime = void>
bool register_component_storage(ComponentRegistry& registry)
{
    using Storage = ecs_detail::ComponentStorage<Data>;
    if (registry.native_world() == nullptr || ecs_is_stage(registry.native_world()) ||
        ecs_stage_is_readonly(registry.native_world()) || registry.contains(T::component_type) ||
        T::component_type == ComponentType::NONE)
    {
        return false;
    }
    flecs::world world(registry.native_world());
    world.component<Storage>();
    if constexpr (!std::is_void_v<Runtime>)
    {
        world.component<Runtime>();
    }
    ComponentRegistry::Operations ops;
    ops.state = [](Node node) -> ComponentState*
    {
        if (!node)
        {
            return nullptr;
        }
        flecs::world current(node.world()->native_world());
        auto* storage = current.entity(node.id()).template try_get_mut<Storage>();
        return storage ? &storage->state : nullptr;
    };
    ops.data = [](Node node) -> void*
    {
        if (!node)
        {
            return nullptr;
        }
        flecs::world current(node.world()->native_world());
        auto* storage = current.entity(node.id()).template try_get_mut<Storage>();
        return storage ? &storage->data : nullptr;
    };
    ops.runtime = [](Node node) -> void*
    {
        if constexpr (std::is_void_v<Runtime>)
        {
            (void)node;
            return nullptr;
        }
        else
        {
            if (!node)
            {
                return nullptr;
            }
            flecs::world current(node.world()->native_world());
            return current.entity(node.id()).template try_get_mut<Runtime>();
        }
    };
    ops.create = [](Node node) -> Component
    {
        if (!node || !node.world()->can_modify())
        {
            return {};
        }
        flecs::world current(node.world()->native_world());
        auto entity = current.entity(node.id());
        if (entity.template has<Storage>())
        {
            return {};
        }
        const auto instance = next_component_instance_id();
        if (instance == 0)
        {
            return {};
        }
        entity.template add<Storage>();
        if constexpr (!std::is_void_v<Runtime>)
        {
            entity.template add<Runtime>();
        }
        auto* storage = entity.template try_get_mut<Storage>();
        storage->state.instance_id = instance;
        Component result(node, T::component_type, instance);
        if (!T(result).initialize())
        {
            if constexpr (!std::is_void_v<Runtime>)
            {
                entity.template remove<Runtime>();
            }
            entity.template remove<Storage>();
            return {};
        }
        node.world()->invalidate_scene_state();
        return result;
    };
    ops.remove = [](Node node) -> bool
    {
        if (!node || !node.world()->can_modify())
        {
            return false;
        }
        flecs::world current(node.world()->native_world());
        auto entity = current.entity(node.id());
        if (!entity.template has<Storage>())
        {
            return false;
        }
        if constexpr (!std::is_void_v<Runtime>)
        {
            entity.template remove<Runtime>();
        }
        entity.template remove<Storage>();
        node.world()->invalidate_scene_state();
        return true;
    };
    ops.snapshot = [](Component component, schema::ComponentDataT& output) -> bool
    {
        output = {};
        const auto* value = component.template read_data<Data>();
        const auto* state = component.state();
        if (!value || !state)
        {
            return false;
        }
        output.flags = std::make_unique<schema::ComponentFlagsT>(state->flags);
        output.payload.Set(Data(*value));
        return true;
    };
    ops.build = [](Component component, schema::ComponentDataT& output) -> bool
    {
        if constexpr (std::is_same_v<decltype(&T::build_data), decltype(&Component::build_data)>)
        {
            return component.get_data(output);
        }
        else
        {
            return T(component).build_data(output);
        }
    };
    ops.restore = [](Component component, const schema::ComponentDataT& input) -> bool
    {
        if (!input.flags || input.payload.type != T::component_type ||
            input.payload.value == nullptr)
        {
            return false;
        }
        return T(component).restore_data(input);
    };
    ops.remap = [](Component component, schema::ComponentDataT& input,
                   std::span<const schema::NodeIdMappingT> mapping) -> bool
    {
        if constexpr (std::is_same_v<decltype(&T::remap_node_references),
                                     decltype(&Component::remap_node_references)>)
        {
            (void)component;
            (void)mapping;
            return input.payload.type == T::component_type && input.payload.value != nullptr;
        }
        else
        {
            return T(component).remap_node_references(input, mapping);
        }
    };
    return registry.register_type(T::component_type, ops);
}
} // namespace engine
