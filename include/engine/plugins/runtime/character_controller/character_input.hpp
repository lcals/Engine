#pragma once
#include "runtime/scene/scene_types.hpp"
#include <string>
#include <vector>
namespace engine::plugins::character_controller
{
struct CharacterActionInput
{
    std::string name;
    bool held = false;
    bool pressed = false;
};
struct CharacterInput
{
    Vec2 move{};
    Vec2 look_delta{};
    Vec2 look_rate{};
    bool toggle_view = false;
    std::vector<CharacterActionInput> actions;
};
} // namespace engine::plugins::character_controller
