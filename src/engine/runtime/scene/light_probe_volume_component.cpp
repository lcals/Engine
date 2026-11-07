#include "runtime/scene/light_probe_volume_component.hpp"

#include "runtime/resource/types/light_probe_resource.hpp"
#include "runtime/scene/light_probe_volume_component_runtime.hpp"

#include "runtime/scene/ecs_storage.hpp"

#include "runtime/scene/node.hpp"
#include "runtime/scene/resource_binding.hpp"
#include "runtime/scene/transform.hpp"

#include <array>
#include <cmath>
#include <utility>

namespace engine
{
bool LightProbeVolumeComponent::register_type(ComponentRegistry& registry)
{
    return register_component_storage<LightProbeVolumeComponent, Data, Runtime>(registry);
}

LightProbeVolumeComponent::Runtime* LightProbeVolumeComponent::runtime() const noexcept
{
    return runtime_state<Runtime>();
}

LightProbeVolumeSettings LightProbeVolumeComponent::settings() const noexcept
{
    Data output{};
    (void)get_settings(output);
    return output;
}

bool LightProbeVolumeComponent::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool LightProbeVolumeComponent::set_settings(const LightProbeVolumeSettings& settings)
{
    if (!writable())
    {
        return false;
    }
    resource_detail::ResourceReference replacement;
    if (!settings.probes_guid.empty())
    {
        auto* resources = node().world()->resource_loader();
        if (!resources || !GuidMap::valid_guid(settings.probes_guid) ||
            !ecs_detail::acquire_resource(node(), settings.probes_guid, replacement) ||
            !replacement.data<LightProbeResource>())
        {
            return false;
        }
    }
    *write_data<Data>() = settings;
    modified();
    runtime()->resource_ = std::move(replacement);
    return true;
}

ResourceHandle LightProbeVolumeComponent::resource() const noexcept
{
    if (!valid())
    {
        return {};
    }
    const auto* probes =
        runtime()->resource_ ? runtime()->resource_.data<LightProbeResource>() : nullptr;
    auto transform = node().get_component<Transform>();
    std::array<float, 16> matrix{};
    if (!probes || probes->baked_transform.size() != matrix.size() || !transform ||
        !transform.world_matrix(matrix.data()))
    {
        return {};
    }
    for (std::size_t index = 0; index < matrix.size(); ++index)
    {
        if (std::abs(matrix[index] - probes->baked_transform[index]) > 0.00001F)
        {
            return {};
        }
    }
    return runtime()->resource_.handle();
}

bool LightProbeVolumeComponent::restore_data(const schema::ComponentDataT& data)
{
    if (!data.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* settings = data.payload.AsLightProbeVolumeData();
    if (!settings || !set_settings(*settings))
    {
        return false;
    }
    if (!restore_flags(data))
    {
        return false;
    }
    return true;
}
} // namespace engine
