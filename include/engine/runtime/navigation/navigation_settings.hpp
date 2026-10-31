#pragma once

#include "runtime/scene/scene_types.hpp"

#include <cstdint>

namespace engine
{

struct NavigationSettings
{
    float cell_size = 0.25F;
    float cell_height = 0.1F;
    float agent_height = 1.8F;
    float agent_radius = 0.35F;
    float agent_max_climb = 0.4F;
    float agent_max_slope = 45.0F;
    std::uint32_t tile_size = 32;
    Vec3 query_half_extents{0.5, 2, 0.5};
};

} // namespace engine
