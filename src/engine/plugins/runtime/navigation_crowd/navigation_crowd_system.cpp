#include "plugins/runtime/navigation_crowd/navigation_crowd_system.hpp"

#include "plugins/runtime/navigation_crowd/navigation_agent.hpp"
#include "runtime/navigation/navigation_system.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#include <cmath>
#include <numbers>
#include <unordered_map>

namespace engine::plugins::navigation_crowd
{
namespace
{

bool same_position(const Vec3& left, const Vec3& right) noexcept
{
    return left.x() == right.x() && left.y() == right.y() && left.z() == right.z();
}

bool can_drive(const Node& node, const Transform* transform) noexcept
{
    if (node.parent() != nullptr || transform == nullptr || !transform->enabled())
    {
        return false;
    }
    const auto& scale = transform->settings().scale;
    const auto* motor = node.get_component<CharacterMotor>();
    const auto* collider = node.get_component<Collider>();
    const auto* body = node.get_component<RigidBody>();
    const bool collider_enabled = collider != nullptr && collider->enabled();
    const bool body_enabled = body != nullptr && body->enabled();
    const bool physics_mirror = collider_enabled && body_enabled &&
                                body->settings().motion == schema::BodyMotion::kinematic &&
                                same_position(body->settings().linear_velocity, Vec3{}) &&
                                same_position(body->settings().angular_velocity, Vec3{});
    return scale.x() == 1.0 && scale.y() == 1.0 && scale.z() == 1.0 &&
           (motor == nullptr || !motor->enabled()) &&
           ((!collider_enabled && !body_enabled) || physics_mirror);
}

NavigationAgentSettings agent_settings(const schema::NavigationAgentDataT& value) noexcept
{
    NavigationAgentSettings result;
    result.radius = value.radius;
    result.height = value.height;
    result.max_speed = value.max_speed;
    result.max_acceleration = value.max_acceleration;
    result.separation_weight = value.separation_weight;
    result.arrival_distance = value.arrival_distance;
    return result;
}

} // namespace

struct NavigationCrowdSystem::State
{
    struct Entry
    {
        NavigationAgentHandle handle{};
        Vec3 last_position{};
        std::uint64_t navigation_revision = 0;
        std::uint64_t settings_revision = 0;
        std::uint64_t request_revision = 0;
        // 只在当前主线程 tick 内借用；下一次 tick 先清除，绝不解引用旧场景对象。
        NavigationAgent* component = nullptr;
        bool requested_target = false;
        bool attempted = false;
    };

    std::unordered_map<std::uint64_t, Entry> agents;
    std::uint64_t scene_id = 0;
    bool retry_failed_agents = false;
};

NavigationCrowdSystem::NavigationCrowdSystem(WorldSystem& world, NavigationSystem& navigation,
                                             std::uint32_t capacity) noexcept
    : world_(world), navigation_(navigation), capacity_(capacity)
{
}

NavigationCrowdSystem::~NavigationCrowdSystem() noexcept
{
    shutdown();
}

bool NavigationCrowdSystem::on_initialize(const EngineConfig&)
{
    if (capacity_ == 0 || capacity_ > 4096)
    {
        return false;
    }
    auto& registry = world_.component_registry();
    if (!registry.contains(NavigationAgent::component_type) && !registry.add<NavigationAgent>())
    {
        return false;
    }
    state_ = std::make_unique<State>();
    return true;
}

void NavigationCrowdSystem::release_agents() noexcept
{
    if (state_ == nullptr)
    {
        return;
    }
    for (auto& [id, entry] : state_->agents)
    {
        // 完整构建/clear 已使句柄失效时无需再次移除。
        if (!navigation_.remove_agent(entry.handle))
        {
            entry.handle = {};
        }
    }
    state_->agents.clear();
    state_->scene_id = 0;
    state_->retry_failed_agents = false;
}

bool NavigationCrowdSystem::on_tick(double delta_time)
{
    ENGINE_PROFILE_ZONE("NavigationCrowd.Sync");
    auto* scene = world_.scene();
    if (scene == nullptr || state_->scene_id != scene->id())
    {
        release_agents();
        if (scene == nullptr)
        {
            return true;
        }
        state_->scene_id = scene->id();
    }
    if (world_.paused())
    {
        return true;
    }
    for (auto& [id, entry] : state_->agents)
    {
        entry.component = nullptr;
    }
    const bool available = navigation_.ready() && navigation_.configure_crowd(capacity_);
    for (const auto& node : scene->nodes())
    {
        auto* component = node->get_component<NavigationAgent>();
        if (component == nullptr)
        {
            continue;
        }
        if (!component->enabled())
        {
            component->stop();
            continue;
        }
        auto* transform = node->get_component<Transform>();
        if (!available || !can_drive(*node, transform))
        {
            component->state_ = NavigationAgentStatus::invalid;
            component->velocity_ = {};
            continue;
        }
        auto& entry = state_->agents[component->instance_id()];
        entry.component = component;
    }
    // 先释放旧实例再分配，满容量下同帧替换也能使用刚释放的槽位。
    bool retry = state_->retry_failed_agents;
    state_->retry_failed_agents = false;
    for (auto iterator = state_->agents.begin(); iterator != state_->agents.end();)
    {
        if (iterator->second.component == nullptr)
        {
            if (navigation_.remove_agent(iterator->second.handle))
            {
                retry = true;
            }
            iterator = state_->agents.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }
    if (!available)
    {
        return true;
    }
    for (auto& [id, entry] : state_->agents)
    {
        auto* component = entry.component;
        auto* transform = component->node().get_component<Transform>();
        const Vec3 position = transform->settings().position;
        NavigationAgentState snapshot;
        bool live = navigation_.agent_state(entry.handle, snapshot);
        const bool moved = !same_position(position, entry.last_position);
        const bool changed = entry.settings_revision != component->settings_revision_;
        const bool requested = entry.request_revision != component->request_revision_ ||
                               entry.requested_target != component->has_target_;
        const bool revised = entry.navigation_revision != navigation_.revision();
        if (live && moved)
        {
            // 显式 Transform 位移是传送，不让 Crowd 在下一帧把它拉回旧位置。
            if (navigation_.remove_agent(entry.handle))
            {
                state_->retry_failed_agents = true;
            }
            entry.handle = {};
            live = false;
        }
        bool created = false;
        if (!live && (!entry.attempted || moved || changed || requested || revised || retry))
        {
            entry.attempted = true;
            created = navigation_.create_agent(position, agent_settings(component->settings()),
                                               entry.handle);
            live = created;
        }
        else if (live && changed &&
                 !navigation_.set_agent_settings(entry.handle,
                                                 agent_settings(component->settings())))
        {
            if (navigation_.remove_agent(entry.handle))
            {
                state_->retry_failed_agents = true;
            }
            entry.handle = {};
            live = false;
        }
        entry.last_position = position;
        entry.settings_revision = component->settings_revision_;
        entry.navigation_revision = navigation_.revision();
        entry.request_revision = component->request_revision_;
        entry.requested_target = component->has_target_;
        if (!live)
        {
            component->state_ = NavigationAgentStatus::invalid;
            component->velocity_ = {};
            continue;
        }
        if (created || requested)
        {
            const bool accepted =
                component->has_target_
                    ? navigation_.set_agent_target(entry.handle, component->target_)
                    : navigation_.stop_agent(entry.handle);
            if (!accepted)
            {
                component->state_ = NavigationAgentStatus::unreachable;
                component->velocity_ = {};
            }
        }
    }
    if (!navigation_.step_crowd(delta_time))
    {
        return false;
    }
    for (auto& [id, entry] : state_->agents)
    {
        auto& component = *entry.component;
        NavigationAgentState snapshot;
        if (!navigation_.agent_state(entry.handle, snapshot))
        {
            continue;
        }
        component.state_ = snapshot.status;
        component.velocity_ = snapshot.velocity;
        if (snapshot.status == NavigationAgentStatus::invalid)
        {
            continue;
        }
        auto* transform = component.node().get_component<Transform>();
        auto settings = transform->settings();
        settings.position = snapshot.position;
        if (component.settings().face_velocity &&
            std::hypot(snapshot.velocity.x(), snapshot.velocity.z()) > 0.01)
        {
            settings.rotation = {settings.rotation.x(),
                                 std::atan2(-snapshot.velocity.x(), -snapshot.velocity.z()) *
                                     180.0 / std::numbers::pi,
                                 settings.rotation.z()};
        }
        if (!transform->set_settings(settings))
        {
            component.state_ = NavigationAgentStatus::invalid;
            component.velocity_ = {};
            if (navigation_.remove_agent(entry.handle))
            {
                state_->retry_failed_agents = true;
            }
            entry.handle = {};
            continue;
        }
        entry.last_position = settings.position;
    }
    return true;
}

void NavigationCrowdSystem::on_shutdown() noexcept
{
    release_agents();
    state_.reset();
}

} // namespace engine::plugins::navigation_crowd
