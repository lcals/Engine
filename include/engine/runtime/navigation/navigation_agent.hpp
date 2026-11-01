#pragma once

#include "runtime/scene/scene_types.hpp"

#include <cstdint>

namespace engine
{

struct NavigationAgentHandle
{
    std::uint32_t slot = 0;
    std::uint64_t generation = 0;
};

struct NavigationAgentSettings
{
    float radius = 0.3F;
    float height = 1.8F;
    float max_speed = 3;
    float max_acceleration = 8;
    float separation_weight = 2;
    float arrival_distance = 0.15F;
};

enum class NavigationAgentStatus
{
    idle,
    pending,
    moving,
    arrived,
    unreachable,
    invalid
};

struct NavigationAgentState
{
    NavigationAgentStatus status = NavigationAgentStatus::invalid;
    Vec3 position{};
    Vec3 velocity{};
    Vec3 target{};
    bool has_target = false;
};

} // namespace engine
