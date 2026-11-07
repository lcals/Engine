#include "plugins/runtime/navigation_crowd/navigation_crowd_system.hpp"

#include "plugins/runtime/navigation_crowd/navigation_agent_runtime.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"

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

bool can_drive(const Node& node, Transform transform) noexcept
{
    TransformSettings pose;
    if (node.parent().valid() || !transform.get_settings(pose) || !transform.active())
    {
        return false;
    }
    const auto& scale = pose.scale;
    const auto motor = node.get_component<CharacterMotor>();
    const auto collider = node.get_component<Collider>();
    const auto body = node.get_component<RigidBody>();
    const bool collider_enabled = static_cast<bool>(collider) && collider.active();
    const bool body_enabled = static_cast<bool>(body) && body.active();
    RigidBody::Data body_settings;
    const bool physics_mirror = collider_enabled && body_enabled &&
                                body.get_settings(body_settings) &&
                                body_settings.motion == schema::BodyMotion::kinematic &&
                                same_position(body_settings.linear_velocity, Vec3{}) &&
                                same_position(body_settings.angular_velocity, Vec3{});
    return scale.x() == 1.0 && scale.y() == 1.0 && scale.z() == 1.0 &&
           (!motor || !motor.active()) && ((!collider_enabled && !body_enabled) || physics_mirror);
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
        NavigationAgent component = {};
        bool requested_target = false;
        bool attempted = false;
    };

    std::unordered_map<std::uint64_t, Entry> agents;
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
    state_->retry_failed_agents = false;
}

bool NavigationCrowdSystem::on_tick(double delta_time)
{
    if (!world_.refresh_scene_state())
    {
        return false;
    }
    ENGINE_PROFILE_ZONE("NavigationCrowd.Sync");
    if (world_.paused())
    {
        return true;
    }
    for (auto& [id, entry] : state_->agents)
    {
        entry.component = {};
    }
    const bool available = navigation_.ready() && navigation_.configure_crowd(capacity_);
    {
        const auto process =
            [&](Node node,
                const ecs_detail::ComponentStorage<NavigationAgent::Data>& stored) -> bool
        {
            const auto* scene = node.scene();
            (void)scene;
            (void)stored;

            auto component = node.get_component<NavigationAgent>();
            if (!component)
            {
                return true;
            }
            if (!component.active())
            {
                component.stop();
                return true;
            }
            auto transform = node.get_component<Transform>();
            if (!available || !can_drive(node, transform))
            {
                engine::ecs_detail::component_runtime(component)->state_ =
                    NavigationAgentStatus::invalid;
                engine::ecs_detail::component_runtime(component)->velocity_ = {};
                return true;
            }
            auto& entry = state_->agents[component.instance_id()];
            entry.component = component;

            return true;
        };
        auto query = flecs::world(world_.native_world())
                         .query_builder<const ecs_detail::ComponentStorage<NavigationAgent::Data>>()
                         .with<ecs_detail::Published>()
                         .build();
        bool query_success = true;
        query.each(
            [&](flecs::entity entity,
                const ecs_detail::ComponentStorage<NavigationAgent::Data>& stored)
            {
                if (query_success)
                {
                    query_success = process(world_.find_node(entity.id()), stored);
                }
            });
        (void)query_success;
    }
    // 先释放旧实例再分配，满容量下同帧替换也能使用刚释放的槽位。
    bool retry = state_->retry_failed_agents;
    state_->retry_failed_agents = false;
    for (auto iterator = state_->agents.begin(); iterator != state_->agents.end();)
    {
        if (!iterator->second.component)
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
        auto component = entry.component;
        auto transform = component.node().get_component<Transform>();
        TransformSettings pose;
        NavigationAgent::Data component_settings;
        if (!transform.get_settings(pose) || !component.get_settings(component_settings))
        {
            return false;
        }
        const Vec3 position = pose.position;
        NavigationAgentState snapshot;
        bool live = navigation_.agent_state(entry.handle, snapshot);
        const bool moved = !same_position(position, entry.last_position);
        const bool changed = entry.settings_revision !=
                             engine::ecs_detail::component_runtime(component)->settings_revision_;
        const bool requested =
            entry.request_revision !=
                engine::ecs_detail::component_runtime(component)->request_revision_ ||
            entry.requested_target != engine::ecs_detail::component_runtime(component)->has_target_;
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
            created = navigation_.create_agent(position, agent_settings(component_settings),
                                               entry.handle);
            live = created;
        }
        else if (live && changed &&
                 !navigation_.set_agent_settings(entry.handle, agent_settings(component_settings)))
        {
            if (navigation_.remove_agent(entry.handle))
            {
                state_->retry_failed_agents = true;
            }
            entry.handle = {};
            live = false;
        }
        entry.last_position = position;
        entry.settings_revision =
            engine::ecs_detail::component_runtime(component)->settings_revision_;
        entry.navigation_revision = navigation_.revision();
        entry.request_revision =
            engine::ecs_detail::component_runtime(component)->request_revision_;
        entry.requested_target = engine::ecs_detail::component_runtime(component)->has_target_;
        if (!live)
        {
            engine::ecs_detail::component_runtime(component)->state_ =
                NavigationAgentStatus::invalid;
            engine::ecs_detail::component_runtime(component)->velocity_ = {};
            continue;
        }
        if (created || requested)
        {
            const bool accepted =
                engine::ecs_detail::component_runtime(component)->has_target_
                    ? navigation_.set_agent_target(
                          entry.handle, engine::ecs_detail::component_runtime(component)->target_)
                    : navigation_.stop_agent(entry.handle);
            if (!accepted)
            {
                engine::ecs_detail::component_runtime(component)->state_ =
                    NavigationAgentStatus::unreachable;
                engine::ecs_detail::component_runtime(component)->velocity_ = {};
            }
        }
    }
    if (!navigation_.step_crowd(delta_time))
    {
        return false;
    }
    for (auto& [id, entry] : state_->agents)
    {
        auto component = entry.component;
        NavigationAgentState snapshot;
        if (!navigation_.agent_state(entry.handle, snapshot))
        {
            continue;
        }
        engine::ecs_detail::component_runtime(component)->state_ = snapshot.status;
        engine::ecs_detail::component_runtime(component)->velocity_ = snapshot.velocity;
        if (snapshot.status == NavigationAgentStatus::invalid)
        {
            continue;
        }
        auto transform = component.node().get_component<Transform>();
        TransformSettings settings;
        NavigationAgent::Data component_settings;
        if (!transform.get_settings(settings) || !component.get_settings(component_settings))
        {
            return false;
        }
        settings.position = snapshot.position;
        if (component_settings.face_velocity &&
            std::hypot(snapshot.velocity.x(), snapshot.velocity.z()) > 0.01)
        {
            settings.rotation = {settings.rotation.x(),
                                 std::atan2(-snapshot.velocity.x(), -snapshot.velocity.z()) *
                                     180.0 / std::numbers::pi,
                                 settings.rotation.z()};
        }
        if (!transform.set_settings(settings))
        {
            engine::ecs_detail::component_runtime(component)->state_ =
                NavigationAgentStatus::invalid;
            engine::ecs_detail::component_runtime(component)->velocity_ = {};
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
