#pragma once

#include "runtime/physics/physics_object.hpp"
#include "runtime/scene/scene_types.hpp"

#include <cstdint>

namespace engine
{

struct PhysicsImpact
{
    std::uint64_t frame = 0;
    PhysicsObject a{};
    PhysicsObject b{};
    Vec3 point{};
    // 世界空间单位法线，从 a 指向 b。
    Vec3 normal{};
    // 含角速度的碰前法向接近速度，单位 m/s。
    float closing_speed = 0;
    // Jolt EstimateCollisionResponse 的估算法向冲量，单位 N·s；非求解器实际冲量。
    float estimated_impulse = 0;
};

} // namespace engine
