#pragma once

#include "runtime/scene/component.hpp"

namespace engine
{

class PhysicsSystem;

struct RigidBodyRuntime
{
    Vec3 pending_impulse_{};
};

class RigidBody final : public Component
{
public:
    using Data = schema::RigidBodyDataT;
    RigidBody() noexcept = default;
    explicit RigidBody(Component component) noexcept : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    using Runtime = RigidBodyRuntime;
    [[nodiscard]] Runtime* runtime() const noexcept;
    static constexpr ComponentType component_type = ComponentType::RigidBodyData;
    [[nodiscard]] static bool valid_settings(const schema::RigidBodyDataT& settings) noexcept;
    [[nodiscard]] schema::RigidBodyDataT settings() const noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const schema::RigidBodyDataT& settings) noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
    // 动态刚体的一次性世界空间冲量，下一次物理同步消费；不进入持久数据。
    [[nodiscard]] bool add_impulse(const Vec3& impulse) noexcept;
    [[nodiscard]] bool set_linear_velocity(const Vec3& velocity) noexcept;
    [[nodiscard]] Vec3 linear_velocity() const noexcept;
};

} // namespace engine
