#pragma once

#include "runtime/scene/scene_types.hpp"

#include <cstdint>

namespace engine
{

struct PhysicsImpact
{
    std::uint64_t frame = 0;
    std::uint64_t scene_id = 0;
    std::uint64_t node_a = 0;
    std::uint64_t node_b = 0;
    Vec3 point{};
    // 世界空间单位法线，从 node_a 指向 node_b。
    Vec3 normal{};
    // 含角速度的碰前法向接近速度，单位 m/s。
    float closing_speed = 0;
    // Jolt EstimateCollisionResponse 的估算法向冲量，单位 N·s；非求解器实际冲量。
    float estimated_impulse = 0;
};

} // namespace engine
