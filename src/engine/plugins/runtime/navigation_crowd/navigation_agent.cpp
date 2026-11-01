#include "plugins/runtime/navigation_crowd/navigation_agent.hpp"

#include <atomic>
#include <cmath>
#include <limits>

namespace engine::plugins::navigation_crowd
{
namespace
{

schema::ComponentDataT make_data()
{
    schema::ComponentDataT data;
    data.payload.Set(schema::NavigationAgentDataT{});
    return data;
}

std::uint64_t next_instance_id() noexcept
{
    static std::atomic<std::uint64_t> sequence{0};
    auto current = sequence.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<std::uint64_t>::max())
    {
        if (sequence.compare_exchange_weak(current, current + 1, std::memory_order_relaxed))
        {
            return current + 1;
        }
    }
    return 0;
}

bool bounded(float value, float minimum, float maximum) noexcept
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

} // namespace

NavigationAgent::NavigationAgent(Node& node)
    : Component(node, make_data()), instance_id_(next_instance_id())
{
}

bool NavigationAgent::initialize()
{
    return instance_id_ != 0;
}

bool NavigationAgent::valid_settings(const schema::NavigationAgentDataT& value) noexcept
{
    return bounded(value.radius, 0.01F, 10.0F) && bounded(value.height, 0.01F, 100.0F) &&
           bounded(value.max_speed, 0.01F, 1000.0F) &&
           bounded(value.max_acceleration, 0.01F, 1000.0F) &&
           bounded(value.separation_weight, 0.0F, 100.0F) &&
           bounded(value.arrival_distance, 0.01F, 10.0F);
}

const schema::NavigationAgentDataT& NavigationAgent::settings() const noexcept
{
    return *data().payload.AsNavigationAgentData();
}

bool NavigationAgent::set_settings(const schema::NavigationAgentDataT& value) noexcept
{
    if (!valid_settings(value) || settings_revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    *mutable_data().payload.AsNavigationAgentData() = value;
    ++settings_revision_;
    return true;
}

bool NavigationAgent::move_to(const Vec3& target) noexcept
{
    if (!enabled() || !std::isfinite(target.x()) || !std::isfinite(target.y()) ||
        !std::isfinite(target.z()) || std::abs(target.x()) > 1000000.0 ||
        std::abs(target.y()) > 1000000.0 || std::abs(target.z()) > 1000000.0 ||
        request_revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    target_ = target;
    has_target_ = true;
    state_ = NavigationAgentStatus::pending;
    ++request_revision_;
    return true;
}

void NavigationAgent::stop() noexcept
{
    has_target_ = false;
    target_ = {};
    velocity_ = {};
    state_ = NavigationAgentStatus::idle;
    // Stop 仍须可取消请求，即使极端情况下请求计数已耗尽。
    if (request_revision_ != std::numeric_limits<std::uint64_t>::max())
    {
        ++request_revision_;
    }
}

NavigationAgentStatus NavigationAgent::state() const noexcept
{
    return state_;
}

Vec3 NavigationAgent::velocity() const noexcept
{
    return velocity_;
}

std::uint64_t NavigationAgent::instance_id() const noexcept
{
    return instance_id_;
}

bool NavigationAgent::restore_data(const schema::ComponentDataT& value)
{
    const auto* settings = value.payload.AsNavigationAgentData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(value.enabled);
    stop();
    return true;
}

} // namespace engine::plugins::navigation_crowd
