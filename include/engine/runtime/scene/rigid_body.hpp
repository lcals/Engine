#pragma once

#include "runtime/scene/component.hpp"

namespace engine
{

class PhysicsSystem;

class RigidBody final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::RigidBodyData;
    [[nodiscard]] static bool valid_settings(const schema::RigidBodyDataT& settings) noexcept;
    [[nodiscard]] const schema::RigidBodyDataT& settings() const noexcept;
    [[nodiscard]] bool set_settings(const schema::RigidBodyDataT& settings) noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;
    // 动态刚体的一次性世界空间冲量，下一次物理同步消费；不进入持久数据。
    [[nodiscard]] bool add_impulse(const Vec3& impulse) noexcept;
    [[nodiscard]] bool set_linear_velocity(const Vec3& velocity) noexcept;
    [[nodiscard]] const Vec3& linear_velocity() const noexcept;

private:
    friend class Node;
    friend class PhysicsSystem;
    explicit RigidBody(Node& node);
    Vec3 pending_impulse_{};
};

} // namespace engine
