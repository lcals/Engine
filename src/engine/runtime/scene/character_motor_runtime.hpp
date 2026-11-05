#pragma once

#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/component_runtime.hpp"

namespace engine
{
struct CharacterMotorRuntime
{
    Vec3 desired_velocity_{};
    Vec3 velocity_{};
    Vec3 pending_velocity_{};
    float pending_jump_ = 0.0F;
    bool grounded_ = false;
    bool reset_pending_ = false;
};
} // namespace engine
