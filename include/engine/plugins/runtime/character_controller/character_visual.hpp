#pragma once

#include "runtime/scene/component.hpp"

namespace engine::plugins::character_controller
{

class CharacterVisual final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::CharacterVisualData;
    [[nodiscard]] static bool valid_settings(const schema::CharacterVisualDataT& settings) noexcept;
    [[nodiscard]] const schema::CharacterVisualDataT& settings() const noexcept;
    [[nodiscard]] bool set_settings(const schema::CharacterVisualDataT& settings) noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;

private:
    friend class engine::Node;
    explicit CharacterVisual(Node& node);
};

} // namespace engine::plugins::character_controller
