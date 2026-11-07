#include "runtime/scene/component.hpp"

#include "runtime/scene/component_registry.hpp"
#include "runtime/world/world_system.hpp"

#include <atomic>
#include <limits>

namespace engine
{
std::uint64_t next_component_instance_id() noexcept
{
    static std::atomic<std::uint64_t> sequence{0};
    auto current = sequence.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<std::uint64_t>::max())
    {
        if (sequence.compare_exchange_weak(current, current + 1, std::memory_order_relaxed))
        {
            return current + 1;
        }
    }
    return 0;
}

Component::Component(Node node, ComponentType type, std::uint64_t instance) noexcept
    : node_(node), type_(type), instance_id_(instance)
{
}

Component::Component(Component value, ComponentType expected) noexcept
{
    if (value.type_ == expected)
    {
        *this = value;
    }
}

ComponentState* Component::state() const noexcept
{
    if (!node_ || instance_id_ == 0)
    {
        return nullptr;
    }
    const auto* ops = node_.world()->component_registry().operations(type_);
    auto* state = ops ? ops->state(node_) : nullptr;
    return state && state->instance_id == instance_id_ ? state : nullptr;
}

bool Component::valid() const noexcept
{
    return state() != nullptr;
}
bool Component::writable() const noexcept
{
    return valid() && node_.world()->can_modify();
}
Node Component::node() const noexcept
{
    return node_;
}
std::uint64_t Component::instance_id() const noexcept
{
    return instance_id_;
}
ComponentType Component::type() const noexcept
{
    return type_;
}

bool Component::enabled() const noexcept
{
    const auto* current = state();
    return current && current->flags.enabled;
}
bool Component::set_enabled(bool value) noexcept
{
    if (!writable())
    {
        return false;
    }
    state()->flags.enabled = value;
    modified();
    return true;
}
bool Component::active() const noexcept
{
    return enabled() && node_.active();
}
bool Component::visible() const noexcept
{
    const auto* current = state();
    return current && current->flags.visible;
}
bool Component::set_visible(bool value) noexcept
{
    if (!writable())
    {
        return false;
    }
    state()->flags.visible = value;
    modified();
    return true;
}
bool Component::visible_in_hierarchy() const noexcept
{
    return visible() && node_.visible_in_hierarchy();
}
bool Component::transient() const noexcept
{
    const auto* current = state();
    return current && current->transient;
}
bool Component::set_transient(bool value) noexcept
{
    if (!writable())
    {
        return false;
    }
    state()->transient = value;
    return true;
}
void Component::modified() noexcept
{
    auto* current = state();
    if (!current)
    {
        return;
    }
    ++current->revision;
    if (type_ == ComponentType::TransformData || type_ == ComponentType::AnimatorData ||
        type_ == ComponentType::BoneAttachmentData)
    {
        node_.world()->invalidate_scene_state();
    }
}
bool Component::restore_flags(const schema::ComponentDataT& data) noexcept
{
    if (!data.flags || !writable())
    {
        return false;
    }
    state()->flags = *data.flags;
    modified();
    return true;
}
const void* Component::read_value() const noexcept
{
    if (!valid())
    {
        return nullptr;
    }
    return node_.world()->component_registry().operations(type_)->data(node_);
}
void* Component::write_value() noexcept
{
    return writable() ? node_.world()->component_registry().operations(type_)->data(node_)
                      : nullptr;
}
void* Component::runtime_value() const noexcept
{
    if (!valid())
    {
        return nullptr;
    }
    return node_.world()->component_registry().operations(type_)->runtime(node_);
}
bool Component::get_data(schema::ComponentDataT& output) const
{
    output = {};
    return valid() &&
           node_.world()->component_registry().operations(type_)->snapshot(*this, output);
}
bool Component::build_data(schema::ComponentDataT& output) const
{
    output = {};
    return valid() && node_.world()->component_registry().operations(type_)->build(*this, output);
}
bool Component::restore_data(const schema::ComponentDataT& data)
{
    return writable() &&
           node_.world()->component_registry().operations(type_)->restore(*this, data);
}
bool Component::remap_node_references(schema::ComponentDataT& data,
                                      std::span<const schema::NodeIdMappingT> mapping) const
{
    return valid() &&
           node_.world()->component_registry().operations(type_)->remap(*this, data, mapping);
}
} // namespace engine
