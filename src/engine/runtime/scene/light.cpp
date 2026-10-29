#include "runtime/scene/light.hpp"

#include <cmath>

namespace engine
{
namespace
{

bool unit_value(float value)
{
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

bool valid_color(const Color& color)
{
    return unit_value(color.r()) && unit_value(color.g()) && unit_value(color.b()) &&
           unit_value(color.a());
}

} // namespace

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
    if (!valid_color(settings.color) || !std::isfinite(settings.intensity) ||
        settings.intensity < 0.0F)
    {
        return false;
    }
    *mutable_data().payload.AsLightData() = settings;
    return true;
}

} // namespace engine
