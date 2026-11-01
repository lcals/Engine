#pragma once

#include "runtime/physics/physics_object.hpp"
#include "runtime/scene/scene_types.hpp"

#include <cstdint>

namespace engine
{

enum class PhysicsContactPhase : std::uint8_t
{
    begin,
    stay,
    end
};

struct PhysicsContactEvent
{
    std::uint64_t frame = 0;
    std::uint64_t step = 0;
    PhysicsContactPhase phase = PhysicsContactPhase::begin;
    PhysicsObject a{};
    PhysicsObject b{};
    Vec3 point{};
    // 世界法线从 a 指向 b；end 保留最后一次有效接触的点与法线。
    Vec3 normal{};
    bool is_trigger = false;
};

} // namespace engine
