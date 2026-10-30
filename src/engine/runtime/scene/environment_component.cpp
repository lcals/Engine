#include "runtime/scene/environment_component.hpp"

#include "runtime/scene/scene_settings_validation.hpp"

namespace engine
{

EnvironmentComponent::EnvironmentComponent(Node& node) : Component(node)
{
    mutable_data().payload.Set(EnvironmentSettings{});
}

const EnvironmentSettings& EnvironmentComponent::settings() const noexcept
{
    return *data().payload.AsEnvironmentData();
}

bool EnvironmentComponent::set_settings(const EnvironmentSettings& settings)
{
    if (!scene_detail::valid_environment(settings))
    {
        return false;
    }
    *mutable_data().payload.AsEnvironmentData() = settings;
    return true;
}

bool EnvironmentComponent::restore_data(const schema::ComponentDataT& data)
{
    const auto* settings = data.payload.AsEnvironmentData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(data.enabled);
    return true;
}

} // namespace engine
