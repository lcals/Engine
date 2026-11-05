#pragma once

#include "plugins/runtime/navigation_crowd/navigation_agent.hpp"
#include "runtime/scene/component_runtime.hpp"

namespace engine::plugins::navigation_crowd
{
struct NavigationAgentRuntime
{
    std::uint64_t settings_revision_ = 1;
    std::uint64_t request_revision_ = 1;
    Vec3 target_{};
    Vec3 velocity_{};
    bool has_target_ = false;
    NavigationAgentStatus state_ = NavigationAgentStatus::idle;
};
} // namespace engine::plugins::navigation_crowd
