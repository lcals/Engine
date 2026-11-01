#pragma once

#include "runtime/scene/scene_types.hpp"

#include <cstdint>

namespace engine
{

struct PhysicsQueryFilter
{
    std::uint64_t ignore_node_id = 0;
    bool ignore_descendants = true;
    bool include_static = true;
    bool include_kinematic = true;
    bool include_dynamic = true;
    bool include_sensors = false;
};

struct PhysicsQueryHit
{
    bool hit = false;
    std::uint64_t scene_id = 0;
    std::uint64_t node_id = 0;
    std::uint64_t collider_id = 0;
    Vec3 point{};
    Vec3 normal{};
    // 球扫为球心行程；point 为目标上的接触点，不是球心。
    float distance = 0;
    float fraction = 0;
};

} // namespace engine
