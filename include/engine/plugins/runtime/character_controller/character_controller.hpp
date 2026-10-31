#pragma once

#include "runtime/scene/component.hpp"

#include <cstdint>

namespace engine::plugins::character_controller
{

class CharacterController final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::CharacterControllerData;
    [[nodiscard]] static bool
    valid_settings(const schema::CharacterControllerDataT& settings) noexcept;
    [[nodiscard]] const schema::CharacterControllerDataT& settings() const noexcept;
    [[nodiscard]] bool set_settings(const schema::CharacterControllerDataT& settings) noexcept;
    [[nodiscard]] std::uint64_t instance_id() const noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;

private:
    friend class engine::Node;
    explicit CharacterController(Node& node);
    [[nodiscard]] bool initialize() override;
    const std::uint64_t instance_id_;
};

} // namespace engine::plugins::character_controller
