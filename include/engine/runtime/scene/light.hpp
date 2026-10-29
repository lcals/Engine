#pragma once

#include "runtime/scene/component.hpp"

namespace engine
{

class Light final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::LightData;

    [[nodiscard]] const LightSettings& settings() const noexcept;
    [[nodiscard]] bool set_settings(const LightSettings& settings) noexcept;

private:
    friend class Node;
    explicit Light(Node& node);
};

} // namespace engine
