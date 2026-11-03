#pragma once

#include "runtime/scene/component.hpp"

namespace engine
{

class PlanarReflectionComponent final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::PlanarReflectionData;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;

    [[nodiscard]] const PlanarReflectionSettings& settings() const noexcept;
    [[nodiscard]] bool set_settings(const PlanarReflectionSettings& settings) noexcept;

private:
    friend class Node;
    explicit PlanarReflectionComponent(Node& node);
};

} // namespace engine
