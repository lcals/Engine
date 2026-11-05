#pragma once

#include "plugins/runtime/character_controller/character_camera.hpp"
#include "runtime/scene/component_runtime.hpp"

namespace engine::plugins::character_controller
{
struct CharacterCameraRuntime
{
    double yaw_ = 0.0;
    double pitch_ = 0.0;
    bool angles_initialized_ = false;
};
} // namespace engine::plugins::character_controller
