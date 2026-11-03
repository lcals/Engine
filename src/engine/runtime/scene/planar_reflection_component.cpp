#include "runtime/scene/planar_reflection_component.hpp"

#include "runtime/scene/scene_settings_validation.hpp"

namespace engine
{

PlanarReflectionComponent::PlanarReflectionComponent(Node& node) : Component(node)
{
    mutable_data().payload.Set(PlanarReflectionSettings{});
}

const PlanarReflectionSettings& PlanarReflectionComponent::settings() const noexcept
{
    return *data().payload.AsPlanarReflectionData();
}

bool PlanarReflectionComponent::set_settings(const PlanarReflectionSettings& settings) noexcept
{
    if (!scene_detail::valid_planar_reflection(settings))
    {
        return false;
    }
    *mutable_data().payload.AsPlanarReflectionData() = settings;
    return true;
}

bool PlanarReflectionComponent::restore_data(const schema::ComponentDataT& data)
{
    const auto* settings = data.payload.AsPlanarReflectionData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(data.enabled);
    return true;
}

} // namespace engine
