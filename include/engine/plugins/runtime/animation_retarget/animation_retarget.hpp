#pragma once

#include "runtime/animation/animation_retargeter.hpp"

namespace engine::plugins::animation_retarget
{
[[nodiscard]] std::unique_ptr<AnimationRetargeter> create_animation_retargeter();
} // namespace engine::plugins::animation_retarget
