#include "runtime/scene/node.hpp"

#include "runtime/scene/component_registry.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <flecs.h>

namespace engine
{
Node::Node(WorldSystem& world, std::uint64_t epoch, std::uint64_t entity) noexcept
    : world_(&world), world_epoch_(epoch), entity_id_(entity)
{
}

bool Node::valid() const noexcept
{
    if (!world_ || entity_id_ == 0 || world_epoch_ != world_->epoch() ||
        world_->native_world() == nullptr || !ecs_is_alive(world_->native_world(), entity_id_))
    {
        return false;
    }
    flecs::world current(world_->native_world());
    return current.entity(entity_id_).has<ecs_detail::NodeRuntime>();
}
std::uint64_t Node::id() const noexcept
{
    return entity_id_;
}
WorldSystem* Node::world() const noexcept
{
    return world_ && world_->epoch() == world_epoch_ && world_->native_world() ? world_ : nullptr;
}
Scene* Node::scene() const noexcept
{
    if (!valid())
    {
        return nullptr;
    }
    flecs::world current(world_->native_world());
    const auto* state = current.entity(entity_id_).try_get<ecs_detail::NodeRuntime>();
    return state ? state->scene : nullptr;
}
std::string Node::name() const
{
    if (!valid())
    {
        return {};
    }
    flecs::world current(world_->native_world());
    const auto* info = current.entity(entity_id_).try_get<schema::NodePropertiesT>();
    return info ? info->name : std::string{};
}
std::uint64_t Node::local_id() const noexcept
{
    if (!valid())
    {
        return 0;
    }
    flecs::world current(world_->native_world());
    const auto* properties = current.entity(entity_id_).try_get<schema::NodePropertiesT>();
    return properties ? properties->id : 0;
}
bool Node::build_info(schema::NodeInfoT& output) const
{
    output = {};
    if (!valid())
    {
        return false;
    }
    flecs::world current(world_->native_world());
    const auto* info = current.entity(entity_id_).try_get<schema::NodePropertiesT>();
    if (!info)
    {
        return false;
    }
    output.properties = std::make_unique<schema::NodePropertiesT>(*info);
    const auto parent_entity = current.entity(entity_id_).parent();
    if (parent_entity)
    {
        const auto* parent_info = parent_entity.try_get<schema::NodePropertiesT>();
        if (!parent_info)
        {
            output = {};
            return false;
        }
        output.parent_id = parent_info->id;
    }
    return true;
}
bool Node::enabled() const noexcept
{
    if (!valid())
    {
        return false;
    }
    flecs::world current(world_->native_world());
    const auto* info = current.entity(entity_id_).try_get<schema::NodePropertiesT>();
    return info && info->enabled;
}
bool Node::set_enabled(bool value) noexcept
{
    if (!valid() || !world_->can_modify())
    {
        return false;
    }
    flecs::world current(world_->native_world());
    auto* info = current.entity(entity_id_).try_get_mut<schema::NodePropertiesT>();
    if (!info)
    {
        return false;
    }
    info->enabled = value;
    world_->invalidate_scene_state();
    current.event<Node>().id<ecs_detail::Published>().entity(entity_id_).ctx(*this).emit();
    return true;
}
bool Node::active() const noexcept
{
    if (!valid() || !world_->refresh_scene_state())
    {
        return false;
    }
    flecs::world current(world_->native_world());
    return current.entity(entity_id_).try_get<ecs_detail::NodeRuntime>()->active;
}
bool Node::visible() const noexcept
{
    if (!valid())
    {
        return false;
    }
    flecs::world current(world_->native_world());
    const auto* info = current.entity(entity_id_).try_get<schema::NodePropertiesT>();
    return info && info->visible;
}
bool Node::set_visible(bool value) noexcept
{
    if (!valid() || !world_->can_modify())
    {
        return false;
    }
    flecs::world current(world_->native_world());
    auto* info = current.entity(entity_id_).try_get_mut<schema::NodePropertiesT>();
    if (!info)
    {
        return false;
    }
    info->visible = value;
    world_->invalidate_scene_state();
    current.event<Node>().id<ecs_detail::Published>().entity(entity_id_).ctx(*this).emit();
    return true;
}
bool Node::visible_in_hierarchy() const noexcept
{
    if (!valid() || !world_->refresh_scene_state())
    {
        return false;
    }
    flecs::world current(world_->native_world());
    return current.entity(entity_id_).try_get<ecs_detail::NodeRuntime>()->visible_in_hierarchy;
}
Node Node::parent() const noexcept
{
    if (!valid())
    {
        return {};
    }
    flecs::world current(world_->native_world());
    const auto parent_entity = current.entity(entity_id_).parent();
    return parent_entity ? Node(*world_, world_epoch_, parent_entity.id()) : Node{};
}
Node Node::find_child(std::string_view relative_path) const noexcept
{
    const auto* owner = scene();
    if (!owner || relative_path.empty() || relative_path.front() == '/' ||
        relative_path.back() == '/')
    {
        return {};
    }
    flecs::world native(world_->native_world());
    auto current = *this;
    while (!relative_path.empty())
    {
        const auto separator = relative_path.find('/');
        const auto segment = relative_path.substr(0, separator);
        if (segment.empty() || segment == "." || segment == "..")
        {
            return {};
        }
        Node found;
        bool ambiguous = false;
        native.entity(current.id())
            .children(
                [&](flecs::entity entity)
                {
                    const auto* properties = entity.try_get<schema::NodePropertiesT>();
                    if (!properties || properties->name != segment)
                    {
                        return;
                    }
                    const auto candidate = owner->find_node(entity.id());
                    if (candidate)
                    {
                        ambiguous = ambiguous || found.valid();
                        found = candidate;
                    }
                });
        if (!found || ambiguous)
        {
            return {};
        }
        current = found;
        if (separator == std::string_view::npos)
        {
            return current;
        }
        relative_path.remove_prefix(separator + 1);
    }
    return {};
}
bool Node::set_parent(Node parent) noexcept
{
    if (!valid() || !world_->can_modify())
    {
        return false;
    }
    if (parent.entity_id_ != 0 &&
        (!parent.valid() || parent.world_ != world_ || parent.world_epoch_ != world_epoch_ ||
         parent.scene() != scene()))
    {
        return false;
    }
    flecs::world current(world_->native_world());
    auto entity = current.entity(entity_id_);
    if (parent && entity.try_get<ecs_detail::NodeRuntime>()->persistent)
    {
        return false;
    }
    for (auto ancestor = parent; ancestor; ancestor = ancestor.parent())
    {
        if (ancestor == *this)
        {
            return false;
        }
    }
    if (parent)
    {
        entity.add(flecs::ChildOf, parent.id());
    }
    else
    {
        entity.remove(flecs::ChildOf, flecs::Wildcard);
    }
    world_->invalidate_scene_state();
    current.event<Node>().id<ecs_detail::Published>().entity(entity_id_).ctx(*this).emit();
    return true;
}
bool Node::transient() const noexcept
{
    for (auto ancestor = *this; ancestor; ancestor = ancestor.parent())
    {
        flecs::world current(ancestor.world_->native_world());
        if (current.entity(ancestor.id()).try_get<ecs_detail::NodeRuntime>()->transient)
        {
            return true;
        }
    }
    return false;
}
bool Node::set_transient(bool value) noexcept
{
    if (!valid() || !world_->can_modify())
    {
        return false;
    }
    flecs::world current(world_->native_world());
    current.entity(entity_id_).try_get_mut<ecs_detail::NodeRuntime>()->transient = value;
    return true;
}
bool Node::persistent() const noexcept
{
    for (auto ancestor = *this; ancestor; ancestor = ancestor.parent())
    {
        flecs::world current(ancestor.world_->native_world());
        if (current.entity(ancestor.id()).try_get<ecs_detail::NodeRuntime>()->persistent)
        {
            return true;
        }
    }
    return false;
}
bool Node::set_persistent(bool value) noexcept
{
    if (!valid() || !world_->can_modify() || parent())
    {
        return false;
    }
    flecs::world current(world_->native_world());
    current.entity(entity_id_).try_get_mut<ecs_detail::NodeRuntime>()->persistent = value;
    return true;
}
Component Node::add_component(ComponentType type)
{
    return valid() ? world_->component_registry().create(type, *this) : Component{};
}
Component Node::get_component(ComponentType type) const noexcept
{
    return valid() ? world_->component_registry().find(type, *this) : Component{};
}
bool Node::remove_component(ComponentType type)
{
    return valid() && world_->component_registry().remove(type, *this);
}
bool Node::collect_components(std::vector<Component>& output) const
{
    output.clear();
    return valid() && world_->component_registry().collect(*this, output);
}
} // namespace engine
