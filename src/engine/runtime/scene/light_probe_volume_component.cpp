#include "runtime/scene/light_probe_volume_component.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/transform.hpp"

#include <array>
#include <cmath>
#include <utility>

namespace engine
{
LightProbeVolumeComponent::LightProbeVolumeComponent(Node& node) : Component(node)
{
    mutable_data().payload.Set(LightProbeVolumeSettings{});
}

const LightProbeVolumeSettings& LightProbeVolumeComponent::settings() const noexcept
{
    return *data().payload.AsLightProbeVolumeData();
}

bool LightProbeVolumeComponent::set_settings(const LightProbeVolumeSettings& settings)
{
    ResourcePtr replacement;
    if (!settings.probes_guid.empty())
    {
        auto* resources = ResourceSystem::instance();
        if (!resources || !GuidMap::valid_guid(settings.probes_guid) ||
            !resources->loader().load(settings.probes_guid, replacement) ||
            !replacement->light_probe())
        {
            return false;
        }
    }
    *mutable_data().payload.AsLightProbeVolumeData() = settings;
    resource_ = std::move(replacement);
    return true;
}

std::shared_ptr<const Resource> LightProbeVolumeComponent::resource() const noexcept
{
    const auto* probes = resource_ ? resource_->light_probe() : nullptr;
    const auto* transform = node().get_component<Transform>();
    std::array<float, 16> matrix{};
    if (!probes || probes->baked_transform.size() != matrix.size() || !transform ||
        !transform->world_matrix(matrix.data()))
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
    return resource_;
}

bool LightProbeVolumeComponent::restore_data(const schema::ComponentDataT& data)
{
    const auto* settings = data.payload.AsLightProbeVolumeData();
    if (!settings || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(data.enabled);
    return true;
}
} // namespace engine
