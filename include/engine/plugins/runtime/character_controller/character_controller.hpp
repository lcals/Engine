#pragma once

#include "runtime/scene/component.hpp"

namespace engine::plugins::character_controller
{

class CharacterControllerSystem;

class CharacterController final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::CharacterControllerData;
    [[nodiscard]] static bool
    valid_settings(const schema::CharacterControllerDataT& settings) noexcept;
    [[nodiscard]] const schema::CharacterControllerDataT& settings() const noexcept;
    [[nodiscard]] bool set_settings(const schema::CharacterControllerDataT& settings) noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;

private:
    friend class engine::Node;
    friend class CharacterControllerSystem;
    explicit CharacterController(Node& node);
    double yaw_ = 0.0;
    double pitch_ = 0.0;
    bool view_initialized_ = false;
    bool driving_ = false;
};

} // namespace engine::plugins::character_controller
