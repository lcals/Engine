#include "runtime/scene/ui_component.hpp"

#include "runtime/resource/types/ui_resource.hpp"
#include "runtime/scene/ui_component_runtime.hpp"

#include "runtime/scene/ecs_storage.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/scene/resource_binding.hpp"

#include <limits>
#include <utility>

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
    Data output{};
    (void)get_settings(output);
    return output;
}

bool UiComponent::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}
bool UiComponent::set_settings(const schema::UiDataT& value)
{
    return apply_settings(value, {});
}

bool UiComponent::apply_settings(const Data& value, resource_detail::ResourceReference prepared)
{
    if (!writable())
    {
        return false;
    }
    if (!valid_settings(value))
    {
        return false;
    }
    resource_detail::ResourceReference next;
    if (runtime()->document_ && !next.retain(runtime()->document_))
    {
        return false;
    }
    if (value.document_guid != settings().document_guid)
    {
        if (runtime()->document_revision_ == std::numeric_limits<std::uint64_t>::max())
        {
            return false;
        }
        next = std::move(prepared);
        if (!value.document_guid.empty())
        {
            auto* resources = node().world()->resource_loader();
            if (!resources ||
                (!next && !ecs_detail::acquire_resource(node(), value.document_guid, next)) ||
                !next || !next.data<UiResource>())
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
    resource_detail::ResourceReference prepared;
    schema::UiDataT value;
    if (!path.empty() && !ecs_detail::acquire_resource(node(), path, prepared))
    {
        return false;
    }
    if (prepared)
    {
        value.document_guid = prepared->info().guid;
    }
    return apply_settings(value, std::move(prepared));
}
ResourceHandle UiComponent::document() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->document_.handle();
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
