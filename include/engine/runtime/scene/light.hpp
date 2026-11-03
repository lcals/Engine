#pragma once

#include "runtime/scene/component.hpp"

namespace engine
{

class Light final : public Component
{
public:
    using Data = schema::LightDataT;
    Light() noexcept = default;
    explicit Light(Component component) noexcept : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    static constexpr ComponentType component_type = ComponentType::LightData;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);

    [[nodiscard]] LightSettings settings() const noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const LightSettings& settings) noexcept;
};

} // namespace engine
