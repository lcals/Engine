#pragma once

#include "runtime/resource/resource_load.hpp"
#include "runtime/resource/resource_reference.hpp"
#include "runtime/scene/scene_loading.hpp"

#include <memory>
#include <vector>

namespace engine
{
struct GameplaySession::SceneLoadOperation
{
    SceneLoadSpec spec;
    SceneLoadSnapshot snapshot;
    std::vector<std::unique_ptr<ResourceLoad>> resources;
    std::vector<ResourceReference> retained;
    std::uint64_t started_ns = 0;
    std::uint64_t resource_ready_ns = 0;
    bool restored = false;
    bool activate = false;
};
} // namespace engine
