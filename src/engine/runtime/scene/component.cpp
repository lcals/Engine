#include "runtime/scene/component.hpp"
#include "runtime/scene/node.hpp"

#include <atomic>
#include <limits>
#include <utility>

namespace engine
{
namespace
{
std::uint64_t next_instance_id() noexcept
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
} // namespace

Component::Component(Node& node, schema::ComponentDataT data) noexcept
    : node_(node), instance_id_(next_instance_id()), data_(std::move(data))
{
}

Component::~Component() noexcept = default;

std::uint64_t Component::instance_id() const noexcept
{
    return instance_id_;
}

bool Component::active() const noexcept
{
    return enabled() && node_.active();
}

bool Component::visible() const noexcept
{
    return data_.visible;
}

void Component::set_visible(bool visible) noexcept
{
    data_.visible = visible;
}

bool Component::visible_in_hierarchy() const noexcept
{
    return visible() && node_.visible_in_hierarchy();
}

bool Component::initialize()
{
    return true;
}

Node& Component::node() noexcept
{
    return node_;
}

const Node& Component::node() const noexcept
{
    return node_;
}

ComponentType Component::type() const noexcept
{
    return data_.payload.type;
}

bool Component::enabled() const noexcept
{
    return data_.enabled;
}

void Component::set_enabled(bool enabled) noexcept
{
    data_.enabled = enabled;
}

const schema::ComponentDataT& Component::data() const noexcept
{
    return data_;
}

bool Component::transient() const noexcept
{
    return transient_;
}

void Component::set_transient(bool transient) noexcept
{
    transient_ = transient;
}

bool Component::build_data(schema::ComponentDataT& output) const
{
    output = {};
    if (data_.payload.type == ComponentType::NONE || data_.payload.value == nullptr)
    {
        return false;
    }
    output = data_;
    return true;
}

bool Component::remap_node_references(schema::ComponentDataT& data,
                                      std::span<const schema::NodeIdMappingT>) const
{
    return data.payload.type == type() && data.payload.value != nullptr;
}

schema::ComponentDataT& Component::mutable_data() noexcept
{
    return data_;
}

} // namespace engine
