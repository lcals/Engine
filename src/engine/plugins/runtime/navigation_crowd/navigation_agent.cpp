#include "plugins/runtime/navigation_crowd/navigation_agent.hpp"

#include "plugins/runtime/navigation_crowd/navigation_agent_runtime.hpp"

#include "runtime/scene/ecs_storage.hpp"

#include <cmath>
#include <limits>

namespace engine::plugins::navigation_crowd
{
namespace
{

bool bounded(float value, float minimum, float maximum) noexcept
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

} // namespace

bool NavigationAgent::register_type(ComponentRegistry& registry)
{
    return register_component_storage<NavigationAgent, Data, Runtime>(registry);
}

NavigationAgent::Runtime* NavigationAgent::runtime() const noexcept
{
    return runtime_state<Runtime>();
}

bool NavigationAgent::valid_settings(const schema::NavigationAgentDataT& value) noexcept
{
    return bounded(value.radius, 0.01F, 10.0F) && bounded(value.height, 0.01F, 100.0F) &&
           bounded(value.max_speed, 0.01F, 1000.0F) &&
           bounded(value.max_acceleration, 0.01F, 1000.0F) &&
           bounded(value.separation_weight, 0.0F, 100.0F) &&
           bounded(value.arrival_distance, 0.01F, 10.0F);
}

schema::NavigationAgentDataT NavigationAgent::settings() const noexcept
{
    Data output{};
    (void)get_settings(output);
    return output;
}

bool NavigationAgent::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool NavigationAgent::set_settings(const schema::NavigationAgentDataT& value) noexcept
{
    if (!writable())
    {
        return false;
    }
    if (!valid_settings(value) ||
        runtime()->settings_revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    *write_data<Data>() = value;
    modified();
    ++runtime()->settings_revision_;
    return true;
}

bool NavigationAgent::move_to(const Vec3& target) noexcept
{
    if (!writable())
    {
        return false;
    }
    if (!active() || !std::isfinite(target.x()) || !std::isfinite(target.y()) ||
        !std::isfinite(target.z()) || std::abs(target.x()) > 1000000.0 ||
        std::abs(target.y()) > 1000000.0 || std::abs(target.z()) > 1000000.0 ||
        runtime()->request_revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    runtime()->target_ = target;
    runtime()->has_target_ = true;
    runtime()->state_ = NavigationAgentStatus::pending;
    ++runtime()->request_revision_;
    return true;
}

void NavigationAgent::stop() noexcept
{
    if (!writable())
    {
        return;
    }
    runtime()->has_target_ = false;
    runtime()->target_ = {};
    runtime()->velocity_ = {};
    runtime()->state_ = NavigationAgentStatus::idle;
    // Stop 仍须可取消请求，即使极端情况下请求计数已耗尽。
    if (runtime()->request_revision_ != std::numeric_limits<std::uint64_t>::max())
    {
        ++runtime()->request_revision_;
    }
}

NavigationAgentStatus NavigationAgent::state() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->state_;
}

Vec3 NavigationAgent::velocity() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->velocity_;
}

bool NavigationAgent::restore_data(const schema::ComponentDataT& value)
{
    if (!value.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* settings = value.payload.AsNavigationAgentData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    if (!restore_flags(value))
    {
        return false;
    }
    stop();
    return true;
}

} // namespace engine::plugins::navigation_crowd
