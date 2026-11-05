#pragma once

#include "plugins/runtime/terrain/terrain_component.hpp"
#include "runtime/scene/component_runtime.hpp"

namespace engine::plugins::terrain
{
struct TerrainComponentRuntime
{
    ResourcePtr resource_;
    TerrainVolume volume_;
    // Weak tokens identify this exact component even if a node removes and recreates it.
    std::shared_ptr<const std::uint8_t> lifetime_ = std::make_shared<const std::uint8_t>(0);
    std::uint64_t revision_ = 0;
    std::size_t triangles_ = 0;
    bool ready_ = false;
    bool pending_ = false;
    // Remains true if generated nodes were externally removed or the system was shut down.
    // A static-only state must never silently omit previously extracted soil.
    bool detached_ = false;
    std::string last_error_;
};
} // namespace engine::plugins::terrain
