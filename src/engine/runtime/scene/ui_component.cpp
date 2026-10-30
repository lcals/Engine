#include "runtime/scene/ui_component.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/resource_system.hpp"

namespace engine
{
UiComponent::UiComponent(Node& node) : Component(node)
{
    mutable_data().payload.Set(schema::UiDataT{});
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
