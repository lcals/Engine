#pragma once

#include <cstdint>

namespace engine
{

enum class PhysicsObjectKind : std::uint8_t
{
    collider,
    character
};

struct PhysicsObject
{
    std::uint64_t scene_id = 0;
    std::uint64_t node_id = 0;
    PhysicsObjectKind kind = PhysicsObjectKind::collider;
    std::uint64_t instance_id = 0;
};

} // namespace engine
