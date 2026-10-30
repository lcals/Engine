#include "runtime/scene/light.hpp"

#include "runtime/scene/scene_settings_validation.hpp"

namespace engine
{
Light::Light(Node& node) : Component(node)
{
    mutable_data().payload.Set(LightSettings{});
}

const LightSettings& Light::settings() const noexcept
{
    return *data().payload.AsLightData();
}

bool Light::set_settings(const LightSettings& settings) noexcept
{
    if (!scene_detail::valid_light(settings))
    {
        return false;
    }
    *mutable_data().payload.AsLightData() = settings;
    return true;
}

bool Light::restore_data(const schema::ComponentDataT& data)
{
    const auto* settings = data.payload.AsLightData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(data.enabled);
    return true;
}

} // namespace engine
