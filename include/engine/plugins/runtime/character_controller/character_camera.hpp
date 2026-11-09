#pragma once
#include "runtime/scene/component.hpp"
namespace engine::plugins::character_controller
{
class CharacterCamera final : public Component
{
public:
    using Data = schema::CharacterCameraDataT;
    static constexpr ComponentType component_type = ComponentType::CharacterCameraData;
    CharacterCamera() noexcept = default;
    explicit CharacterCamera(Component c) noexcept : Component(c, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    [[nodiscard]] static bool valid_settings(const Data& data) noexcept;
    [[nodiscard]] bool get_settings(Data& output) const;
    [[nodiscard]] bool set_settings(const Data& data);
    [[nodiscard]] bool get_angles(double& yaw, double& pitch) const;
    [[nodiscard]] bool set_angles(double yaw, double pitch);
    [[nodiscard]] bool apply_look(const Vec2& delta, const Vec2& rate, double delta_time);
    [[nodiscard]] bool toggle_view();
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
};
} // namespace engine::plugins::character_controller
