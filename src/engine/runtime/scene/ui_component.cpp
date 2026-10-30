#include "runtime/scene/ui_component.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/resource_system.hpp"

#include <atomic>
#include <limits>

namespace engine
{
UiComponent::UiComponent(Node& node) : Component(node)
{
    static std::atomic<std::uint64_t> sequence{0};
    auto previous = sequence.load(std::memory_order_relaxed);
    while (previous != std::numeric_limits<std::uint64_t>::max())
    {
        if (sequence.compare_exchange_weak(previous, previous + 1, std::memory_order_relaxed))
        {
            view_id_ = previous + 1;
            break;
        }
    }
    mutable_data().payload.Set(schema::UiDataT{});
}
bool UiComponent::initialize()
{
    return view_id_ != 0;
}
std::uint64_t UiComponent::view_id() const noexcept
{
    return view_id_;
}
std::uint64_t UiComponent::document_revision() const noexcept
{
    return document_revision_;
}
bool UiComponent::valid_settings(const schema::UiDataT& settings) noexcept
{
    return settings.document_guid.empty() || GuidMap::valid_guid(settings.document_guid);
}
const schema::UiDataT& UiComponent::settings() const noexcept
{
    return *data().payload.AsUiData();
}
bool UiComponent::set_settings(const schema::UiDataT& value)
{
    if (!valid_settings(value))
    {
        return false;
    }
    ResourcePtr next = document_;
    if (value.document_guid != settings().document_guid)
    {
        if (document_revision_ == std::numeric_limits<std::uint64_t>::max())
        {
            return false;
        }
        next.reset();
        if (!value.document_guid.empty())
        {
            auto* resources = ResourceSystem::instance();
            if (!resources || !resources->loader().load(value.document_guid, next) || !next ||
                !next->ui())
            {
                return false;
            }
        }
        ++document_revision_;
    }
    *mutable_data().payload.AsUiData() = value;
    document_ = std::move(next);
    return true;
}
bool UiComponent::set_document(std::string_view path)
{
    schema::UiDataT value;
    if (!path.empty() && !ResourceSystem::path_to_guid(path, value.document_guid))
    {
        return false;
    }
    return set_settings(value);
}
const ResourcePtr& UiComponent::document() const noexcept
{
    return document_;
}
bool UiComponent::restore_data(const schema::ComponentDataT& value)
{
    const auto* settings = value.payload.AsUiData();
    if (!settings || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(value.enabled);
    return true;
}
} // namespace engine
