#pragma once

#include "runtime/physics/physics_object.hpp"
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
    // 角色为独立类别，不受 include_kinematic 控制。
    bool include_characters = true;
};

struct PhysicsQueryHit
{
    bool hit = false;
    PhysicsObject object{};
    Vec3 point{};
    Vec3 normal{};
    // 球扫为球心行程；point 为目标上的接触点，不是球心。
    float distance = 0;
    float fraction = 0;
};

} // namespace engine
