#pragma once

#include "runtime/scene/component.hpp"

namespace engine
{

class EnvironmentComponent final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::EnvironmentData;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;

    [[nodiscard]] const EnvironmentSettings& settings() const noexcept;
    [[nodiscard]] bool set_settings(const EnvironmentSettings& settings);

private:
    friend class Node;
    explicit EnvironmentComponent(Node& node);
};

} // namespace engine
