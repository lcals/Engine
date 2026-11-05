#pragma once

#include "runtime/scene/component.hpp"

#include <cstdint>

namespace engine
{

class PhysicsSystem;

struct CharacterMotorRuntime;

class CharacterMotor final : public Component
{
public:
    using Data = schema::CharacterMotorDataT;
    CharacterMotor() noexcept = default;
    explicit CharacterMotor(Component component) noexcept : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    using Runtime = CharacterMotorRuntime;
    static constexpr ComponentType component_type = ComponentType::CharacterMotorData;
    [[nodiscard]] static bool valid_settings(const schema::CharacterMotorDataT& settings) noexcept;
    [[nodiscard]] schema::CharacterMotorDataT settings() const noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const schema::CharacterMotorDataT& settings) noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
    // 世界空间水平速度请求，Y 必须为零；不进入持久数据。
    [[nodiscard]] bool set_velocity(const Vec3& velocity) noexcept;
    // 清除主动移动及未消费跳跃，保留外部速度和待消费速度增量。
    void clear_input() noexcept;
    // 世界空间速度增量（m/s）；下一固定步消费一次，与主动输入独立。
    [[nodiscard]] bool add_velocity(const Vec3& delta) noexcept;
    [[nodiscard]] Vec3 velocity() const noexcept;
    [[nodiscard]] bool jump(float speed) noexcept;
    [[nodiscard]] bool grounded() const noexcept;

private:
    template <class T> friend auto engine::ecs_detail::component_runtime(T component) noexcept;
    [[nodiscard]] Runtime* runtime() const noexcept;
};

} // namespace engine
