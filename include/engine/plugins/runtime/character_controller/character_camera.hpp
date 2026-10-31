#pragma once

#include "runtime/scene/component.hpp"

namespace engine::plugins::character_controller
{

class CharacterCamera final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::CharacterCameraData;
    [[nodiscard]] static bool valid_settings(const schema::CharacterCameraDataT& settings) noexcept;
    [[nodiscard]] const schema::CharacterCameraDataT& settings() const noexcept;
    [[nodiscard]] bool set_settings(const schema::CharacterCameraDataT& settings) noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;
    // 首次从本相机 Transform 读取角度；随后由调用方显式设置。
    [[nodiscard]] bool synchronize_angles() noexcept;
    [[nodiscard]] bool set_angles(double yaw, double pitch) noexcept;
    [[nodiscard]] double yaw() const noexcept;
    [[nodiscard]] double pitch() const noexcept;

private:
    friend class engine::Node;
    explicit CharacterCamera(Node& node);
    double yaw_ = 0.0;
    double pitch_ = 0.0;
    bool angles_initialized_ = false;
};

} // namespace engine::plugins::character_controller
