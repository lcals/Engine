#include "runtime/scene/environment_component.hpp"

#include "runtime/resource/types/texture_resource.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/environment_component_runtime.hpp"
#include "runtime/scene/resource_binding.hpp"

#include "runtime/scene/scene_settings_validation.hpp"

namespace engine
{

bool EnvironmentComponent::register_type(ComponentRegistry& registry)
{
    return register_component_storage<EnvironmentComponent, Data, EnvironmentRuntime>(registry);
}

EnvironmentSettings EnvironmentComponent::settings() const noexcept
{
    const auto* value = read_data<Data>();
    return value ? *value : Data{};
}

bool EnvironmentComponent::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool EnvironmentComponent::set_settings(const EnvironmentSettings& settings)
{
    if (!writable())
    {
        return false;
    }
    if (!scene_detail::valid_environment(settings))
    {
        return false;
    }
    EnvironmentRuntime candidate;
    const std::array guids = {&settings.ibl_irradiance_guid, &settings.ibl_prefilter_guid,
                              &settings.ibl_lut_guid};
    const auto current = this->settings();
    const std::array old_guids = {&current.ibl_irradiance_guid, &current.ibl_prefilter_guid,
                                  &current.ibl_lut_guid};
    auto* runtime = runtime_state<EnvironmentRuntime>();
    for (std::size_t index = 0; index < guids.size(); ++index)
    {
        if (guids[index]->empty())
        {
            continue;
        }
        if (runtime->ibl[index] && *old_guids[index] == *guids[index])
        {
            if (!candidate.ibl[index].retain(runtime->ibl[index]))
            {
                return false;
            }
        }
        else if (!ecs_detail::acquire_resource(node(), *guids[index], candidate.ibl[index]))
        {
            return false;
        }
        const auto* texture = candidate.ibl[index].data<TextureResource>();
        if (!texture || texture->cube_map != (index < 2) || texture->srgb ||
            texture->format !=
                (index < 2 ? schema::TextureFormat::rgba16f : schema::TextureFormat::rg16f))
        {
            return false;
        }
    }
    *runtime = std::move(candidate);
    *write_data<Data>() = settings;
    modified();
    return true;
}

bool EnvironmentComponent::restore_data(const schema::ComponentDataT& data)
{
    if (!data.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* settings = data.payload.AsEnvironmentData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    if (!restore_flags(data))
    {
        return false;
    }
    return true;
}

bool EnvironmentComponent::remap_node_references(
    schema::ComponentDataT& data, std::span<const schema::NodeIdMappingT> mapping) const
{
    auto* settings = data.payload.AsEnvironmentData();
    if (!settings)
    {
        return false;
    }
    if (settings->atmosphere_sun_node_id == 0)
    {
        return true;
    }
    for (const auto& item : mapping)
    {
        if (item.source_id == settings->atmosphere_sun_node_id)
        {
            if (item.target_id == 0 || item.retired)
            {
                return false;
            }
            settings->atmosphere_sun_node_id = item.target_id;
            return true;
        }
    }
    return false;
}

} // namespace engine
