#include "runtime/scene/component.hpp"

#include <utility>

namespace engine
{

Component::Component(Node& node, schema::ComponentDataT data) noexcept
    : node_(node), data_(std::move(data))
{
}

Component::~Component() noexcept = default;

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

schema::ComponentDataT& Component::mutable_data() noexcept
{
    return data_;
}

} // namespace engine
