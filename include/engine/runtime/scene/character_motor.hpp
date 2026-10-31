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
    [[nodiscard]] std::uint64_t instance_id() const noexcept;
    // 世界空间水平速度请求，Y 必须为零；不进入持久数据。
    [[nodiscard]] bool set_velocity(const Vec3& velocity) noexcept;
    // 清除主动移动及未消费跳跃，保留外部速度和待消费速度增量。
    void clear_input() noexcept;
    // 世界空间速度增量（m/s）；下一固定步消费一次，与主动输入独立。
    [[nodiscard]] bool add_velocity(const Vec3& delta) noexcept;
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
    Vec3 pending_velocity_{};
    float pending_jump_ = 0.0F;
    bool grounded_ = false;
    bool reset_pending_ = false;
};

} // namespace engine
