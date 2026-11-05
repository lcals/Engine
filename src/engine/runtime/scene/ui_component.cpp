#include "runtime/scene/ui_component.hpp"

#include "runtime/scene/ui_component_runtime.hpp"

#include "runtime/scene/ecs_storage.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/resource_system.hpp"

#include <limits>

namespace engine
{
bool UiComponent::register_type(ComponentRegistry& registry)
{
    return register_component_storage<UiComponent, Data, Runtime>(registry);
}

UiComponent::Runtime* UiComponent::runtime() const noexcept
{
    return runtime_state<Runtime>();
}

std::uint64_t UiComponent::document_revision() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->document_revision_;
}
bool UiComponent::valid_settings(const schema::UiDataT& settings) noexcept
{
    return settings.document_guid.empty() || GuidMap::valid_guid(settings.document_guid);
}
schema::UiDataT UiComponent::settings() const noexcept
{
    const auto* value = read_data<Data>();
    return value ? *value : Data{};
}

bool UiComponent::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}
bool UiComponent::set_settings(const schema::UiDataT& value)
{
    if (!writable())
    {
        return false;
    }
    if (!valid_settings(value))
    {
        return false;
    }
    ResourcePtr next = runtime()->document_;
    if (value.document_guid != settings().document_guid)
    {
        if (runtime()->document_revision_ == std::numeric_limits<std::uint64_t>::max())
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
        ++runtime()->document_revision_;
    }
    *write_data<Data>() = value;
    modified();
    runtime()->document_ = std::move(next);
    return true;
}
bool UiComponent::set_document(std::string_view path)
{
    if (!writable())
    {
        return false;
    }
    schema::UiDataT value;
    if (!path.empty() && !ResourceSystem::path_to_guid(path, value.document_guid))
    {
        return false;
    }
    return set_settings(value);
}
ResourcePtr UiComponent::document() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->document_;
}
bool UiComponent::restore_data(const schema::ComponentDataT& value)
{
    if (!value.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* settings = value.payload.AsUiData();
    if (!settings || !set_settings(*settings))
    {
        return false;
    }
    if (!restore_flags(value))
    {
        return false;
    }
    return true;
}
} // namespace engine
