#pragma once

#include "plugins/runtime/character_controller/character_controller.hpp"
#include "runtime/scene/component_runtime.hpp"

namespace engine::plugins::character_controller
{
struct CharacterControllerRuntime
{
    std::uint64_t driven_motor_ = 0;
    CharacterInput input_{};
};
} // namespace engine::plugins::character_controller
