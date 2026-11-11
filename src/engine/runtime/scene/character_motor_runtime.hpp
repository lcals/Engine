#pragma once

#include "runtime/physics/character_motor_driver.hpp"
#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/component_runtime.hpp"

namespace engine
{
struct CharacterMotorRuntime
{
    CharacterMotorStatus status{};
    Component owner{};
    Vec3 desired_velocity_{};
    Vec3 velocity_{};
    Vec3 pending_velocity_{};
    float pending_jump_ = 0.0F;
    bool grounded_ = false;
    bool reset_pending_ = false;
};

// 只存核心驱动的借用，不包含具体插件；生命周期由物理系统绑定。
struct CharacterMotorWorldRuntime
{
    CharacterMotorDriver* driver = nullptr;
    bool in_callback = false;
};
} // namespace engine
