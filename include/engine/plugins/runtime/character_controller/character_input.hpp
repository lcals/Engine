#pragma once

#include "runtime/scene/scene_types.hpp"

namespace engine::plugins::character_controller
{

struct CharacterInput
{
    // x 向右，y 向前；look 是观察增量，单位由组件灵敏度换算为度。
    Vec2 move{};
    Vec2 look{};
    bool jump = false;
    bool toggle_view = false;
};

} // namespace engine::plugins::character_controller
