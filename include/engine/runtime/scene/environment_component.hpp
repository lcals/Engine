#pragma once

#include "runtime/scene/component.hpp"

namespace engine
{

class EnvironmentComponent final : public Component
{
public:
    using Data = schema::EnvironmentDataT;
    EnvironmentComponent() noexcept = default;
    explicit EnvironmentComponent(Component component) noexcept
        : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    static constexpr ComponentType component_type = ComponentType::EnvironmentData;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);

    [[nodiscard]] EnvironmentSettings settings() const noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const EnvironmentSettings& settings);
};

} // namespace engine
