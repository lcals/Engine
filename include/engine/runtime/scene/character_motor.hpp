#pragma once

#include "runtime/scene/component.hpp"

#include <cstdint>

namespace engine
{

class PhysicsSystem;

class CharacterMotor final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::CharacterMotorData;
    [[nodiscard]] static bool valid_settings(const schema::CharacterMotorDataT& settings) noexcept;
    [[nodiscard]] const schema::CharacterMotorDataT& settings() const noexcept;
    [[nodiscard]] bool set_settings(const schema::CharacterMotorDataT& settings) noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;
    // 世界空间水平速度请求，Y 必须为零；不进入持久数据。
    [[nodiscard]] bool set_velocity(const Vec3& velocity) noexcept;
    [[nodiscard]] const Vec3& velocity() const noexcept;
    [[nodiscard]] bool jump(float speed) noexcept;
    [[nodiscard]] bool grounded() const noexcept;

private:
    friend class Node;
    friend class PhysicsSystem;
    explicit CharacterMotor(Node& node);
    [[nodiscard]] bool initialize() override;
    const std::uint64_t instance_id_;
    Vec3 desired_velocity_{};
    Vec3 velocity_{};
    float pending_jump_ = 0.0F;
    bool grounded_ = false;
};

} // namespace engine
