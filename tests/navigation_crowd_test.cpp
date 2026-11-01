#include "plugins/runtime/navigation_crowd/navigation_agent.hpp"
#include "plugins/runtime/navigation_crowd/navigation_crowd_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/navigation/navigation_system.hpp"
#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/schema/mesh_generated.h"
#include "runtime/world/world_system.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace
{

using engine::NavigationAgentStatus;
using engine::plugins::navigation_crowd::NavigationAgent;
using engine::plugins::navigation_crowd::NavigationCrowdSystem;

bool expect(bool condition, const char* description)
{
    if (!condition)
    {
        std::fprintf(stderr, "navigation crowd test: %s\n", description);
    }
    return condition;
}

struct Fixture
{
    engine::WorldSystem world;
    engine::NavigationSystem navigation;
    NavigationCrowdSystem crowd{world, navigation, 32};
    engine::schema::MeshDataT mesh;

    bool build()
    {
        const std::array geometry{engine::NavigationGeometry{&mesh}};
        return navigation.build(geometry, {{0, -2, 0}, {24, 4, 24}}, {});
    }

    bool initialize()
    {
        mesh.vertices.emplace_back(0, 0, 0, 0, 1, 0);
        mesh.vertices.emplace_back(24, 0, 0, 0, 1, 0);
        mesh.vertices.emplace_back(0, 0, 24, 0, 1, 0);
        mesh.vertices.emplace_back(24, 0, 24, 0, 1, 0);
        mesh.indices = {0, 2, 1, 1, 2, 3};
        return world.initialize({}) && navigation.initialize({}) && crowd.initialize({}) &&
               world.create_scene("Navigation") != nullptr && build();
    }

    NavigationAgent* agent(const engine::Vec3& position)
    {
        auto* node = world.scene()->create_node("Agent");
        if (node == nullptr)
        {
            return nullptr;
        }
        auto* transform = node->get_component<engine::Transform>();
        auto settings = transform->settings();
        settings.position = position;
        return transform->set_settings(settings) ? node->add_component<NavigationAgent>() : nullptr;
    }

    bool advance(unsigned int frames)
    {
        for (unsigned int frame = 0; frame < frames; ++frame)
        {
            if (!crowd.tick(1.0 / 60.0))
            {
                return false;
            }
        }
        return true;
    }
};

engine::Vec3 position(const NavigationAgent& agent)
{
    return agent.node().get_component<engine::Transform>()->settings().position;
}

double distance(const engine::Vec3& left, const engine::Vec3& right)
{
    return std::hypot(left.x() - right.x(), left.z() - right.z());
}

bool requests_and_lifetime()
{
    Fixture fixture;
    if (!expect(fixture.initialize(), "initialize scene/nav/crowd"))
    {
        return false;
    }
    auto* agent = fixture.agent({2, 0, 2});
    if (!expect(
            agent != nullptr && agent->move_to({20, 0, 2}) && fixture.advance(90) &&
                position(*agent).x() > 4 && agent->velocity().x() > 0 &&
                std::abs(agent->node().get_component<engine::Transform>()->settings().rotation.y() +
                         90.0) < 0.1,
            "native crowd writes Transform without script movement"))
    {
        return false;
    }
    const auto settings = agent->settings();
    auto invalid = settings;
    invalid.max_speed = std::numeric_limits<float>::quiet_NaN();
    if (!expect(!agent->set_settings(invalid) &&
                    agent->settings().max_speed == settings.max_speed &&
                    !agent->move_to({std::numeric_limits<double>::quiet_NaN(), 0, 2}),
                "invalid input preserves prior valid settings/request"))
    {
        return false;
    }
    agent->stop();
    const auto stopped = position(*agent);
    if (!expect(fixture.advance(30) && distance(position(*agent), stopped) < 0.001 &&
                    agent->state() == NavigationAgentStatus::idle,
                "stop removes old target and residual speed"))
    {
        return false;
    }
    if (!expect(agent->move_to({2, 0, 2}) && fixture.advance(240) &&
                    agent->state() == NavigationAgentStatus::arrived,
                "target replacement reaches the new destination"))
    {
        return false;
    }
    if (!expect(agent->move_to({100, 0, 2}) && fixture.advance(1) &&
                    agent->state() == NavigationAgentStatus::unreachable,
                "out of mesh target reports unreachable instead of arrival"))
    {
        return false;
    }
    if (!expect(agent->move_to({20, 0, 2}) && fixture.advance(30), "restart after bad target"))
    {
        return false;
    }
    auto* node = &agent->node();
    const auto instance = agent->instance_id();
    const auto before_remove = position(*agent);
    if (!expect(node->remove_component(NavigationAgent::component_type) && fixture.advance(30) &&
                    distance(node->get_component<engine::Transform>()->settings().position,
                             before_remove) < 0.001,
                "removal stops native movement"))
    {
        return false;
    }
    agent = node->add_component<NavigationAgent>();
    if (!expect(agent != nullptr && agent->instance_id() != instance && fixture.advance(30) &&
                    agent->state() == NavigationAgentStatus::idle &&
                    distance(position(*agent), before_remove) < 0.001,
                "replacement component does not inherit a deleted instance target"))
    {
        return false;
    }
    if (!expect(agent->move_to({20, 0, 2}) && fixture.advance(30), "restart replacement"))
    {
        return false;
    }
    agent->set_enabled(false);
    const auto disabled = position(*agent);
    if (!expect(fixture.advance(30) && distance(position(*agent), disabled) < 0.001,
                "disabled component leaves crowd"))
    {
        return false;
    }
    agent->set_enabled(true);
    if (!expect(fixture.advance(30) && agent->state() == NavigationAgentStatus::idle &&
                    distance(position(*agent), disabled) < 0.001,
                "reenabling requires an explicit new request"))
    {
        return false;
    }
    auto* motor = node->add_component<engine::CharacterMotor>();
    if (!expect(motor != nullptr && fixture.advance(1) &&
                    agent->state() == NavigationAgentStatus::invalid,
                "two position writers on one node are rejected"))
    {
        return false;
    }
    motor->set_enabled(false);
    if (!expect(agent->move_to({20, 0, 2}) && fixture.advance(30), "remove ownership conflict"))
    {
        return false;
    }
    fixture.navigation.clear();
    const auto cleared = position(*agent);
    if (!expect(fixture.advance(10) && agent->state() == NavigationAgentStatus::invalid &&
                    distance(position(*agent), cleared) < 0.001 && fixture.build() &&
                    fixture.advance(60) && position(*agent).x() > cleared.x(),
                "clear suspends movement; rebuilt nav recreates valid handles and target"))
    {
        return false;
    }
    auto* transform = node->get_component<engine::Transform>();
    auto pose = transform->settings();
    pose.position = {3, 0, 8};
    if (!expect(transform->set_settings(pose) && fixture.advance(1) && position(*agent).z() > 7,
                "explicit teleport reprojects rather than restoring old crowd position"))
    {
        return false;
    }
    fixture.world.destroy_scene();
    if (!expect(fixture.advance(1) && fixture.world.create_scene("Next") != nullptr,
                "scene deletion releases old agents safely"))
    {
        return false;
    }
    agent = fixture.agent({2, 0, 2});
    return expect(agent != nullptr && fixture.advance(60) &&
                      agent->state() == NavigationAgentStatus::idle && position(*agent).x() < 2.1,
                  "new scene does not reuse old movement");
}

bool many_agents()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    std::vector<NavigationAgent*> agents;
    for (int index = 0; index < 16; ++index)
    {
        const double z = 2.0 + static_cast<double>(index);
        auto* agent = fixture.agent({2, 0, z});
        if (!expect(agent != nullptr, "create multiple agents"))
        {
            return false;
        }
        auto settings = agent->settings();
        // 群体分离允许角色在目标附近停靠；精确到达由前面的单人用例验证。
        settings.arrival_distance = 0.4F;
        if (!expect(agent->set_settings(settings) && agent->move_to({22, 0, z}),
                    "register multiple targets"))
        {
            return false;
        }
        agents.push_back(agent);
    }
    if (!expect(fixture.advance(600), "advance multiple agents in native system"))
    {
        return false;
    }
    for (const auto* agent : agents)
    {
        if (!expect(agent->state() == NavigationAgentStatus::arrived && position(*agent).x() > 21.5,
                    "all 16 independent agents arrive"))
        {
            const auto current = position(*agent);
            const auto velocity = agent->velocity();
            std::fprintf(
                stderr,
                "instance=%llu status=%d position=(%.3f,%.3f,%.3f) velocity=(%.3f,%.3f,%.3f)\n",
                static_cast<unsigned long long>(agent->instance_id()),
                static_cast<int>(agent->state()), current.x(), current.y(), current.z(),
                velocity.x(), velocity.y(), velocity.z());
            return false;
        }
    }
    engine::schema::SceneDataT saved;
    if (!expect(fixture.world.scene()->build_data(saved), "serialize configured agents"))
    {
        return false;
    }
    auto data = agents.front()->data();
    return expect(agents.front()->restore_data(data) &&
                      agents.front()->state() == NavigationAgentStatus::idle &&
                      fixture.advance(1) && agents.front()->state() == NavigationAgentStatus::idle,
                  "restore retains only settings, not the previous movement request");
}

bool capacity_replacement()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    std::vector<NavigationAgent*> agents;
    for (int index = 0; index < 32; ++index)
    {
        auto* agent = fixture.agent({2.0 + (index % 6) * 3.0, 0, 2.0 + (index / 6) * 3.0});
        if (agent == nullptr)
        {
            return false;
        }
        agents.push_back(agent);
    }
    if (!expect(fixture.crowd.tick(0), "fill configured crowd capacity"))
    {
        return false;
    }
    auto* waiting = fixture.agent({20, 0, 20});
    if (!expect(waiting != nullptr && fixture.crowd.tick(0) &&
                    waiting->state() == NavigationAgentStatus::invalid,
                "capacity overflow is reported without discarding existing agents"))
    {
        return false;
    }
    if (!expect(agents[0]->node().remove_component(NavigationAgent::component_type) &&
                    fixture.crowd.tick(0) && waiting->state() == NavigationAgentStatus::idle,
                "released capacity retries a previously blocked registration"))
    {
        return false;
    }
    auto& node = agents[1]->node();
    if (!node.remove_component(NavigationAgent::component_type))
    {
        return false;
    }
    auto* replacement = node.add_component<NavigationAgent>();
    return expect(replacement != nullptr && fixture.crowd.tick(0) &&
                      replacement->state() == NavigationAgentStatus::idle,
                  "same-frame replacement at full capacity frees the old slot first");
}

bool physics_shape_ownership()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto* agent = fixture.agent({2, 0, 6});
    if (!agent || !agent->move_to({20, 0, 6}))
    {
        return false;
    }
    auto& node = agent->node();
    auto* collider = node.add_component<engine::Collider>();
    if (!expect(collider && fixture.advance(1) &&
                    agent->state() == NavigationAgentStatus::invalid,
                "a moving crowd must not silently relocate a standalone static collider"))
    {
        return false;
    }
    auto* body = node.add_component<engine::RigidBody>();
    if (!expect(body && fixture.advance(1) &&
                    agent->state() == NavigationAgentStatus::invalid,
                "a dynamic body conflicts with navigation position ownership"))
    {
        return false;
    }
    auto settings = body->settings();
    settings.motion = engine::schema::BodyMotion::kinematic;
    const auto initial = position(*agent);
    if (!expect(body->set_settings(settings) && fixture.advance(60) &&
                    agent->state() == NavigationAgentStatus::moving &&
                    position(*agent).x() > initial.x(),
                "zero-velocity kinematic collider follows crowd-owned transforms"))
    {
        return false;
    }
    settings.linear_velocity = {1, 0, 0};
    const auto before_conflict = position(*agent);
    if (!expect(body->set_settings(settings) && fixture.advance(10) &&
                    agent->state() == NavigationAgentStatus::invalid &&
                    distance(position(*agent), before_conflict) < 0.001,
                "velocity-driven kinematic bodies cannot also be crowd-driven"))
    {
        return false;
    }
    settings.linear_velocity = {};
    settings.angular_velocity = {0, 1, 0};
    if (!expect(body->set_settings(settings) && fixture.advance(1) &&
                    agent->state() == NavigationAgentStatus::invalid,
                "kinematic angular integration also conflicts with crowd pose ownership"))
    {
        return false;
    }
    settings.angular_velocity = {};
    settings.motion = engine::schema::BodyMotion::static_body;
    if (!expect(body->set_settings(settings) && fixture.advance(1) &&
                    agent->state() == NavigationAgentStatus::invalid,
                "an explicitly static body is not a moving navigation mirror"))
    {
        return false;
    }
    settings.motion = engine::schema::BodyMotion::kinematic;
    if (!body->set_settings(settings))
    {
        return false;
    }
    collider->set_enabled(false);
    if (!expect(fixture.advance(1) && agent->state() == NavigationAgentStatus::invalid,
                "an enabled rigid body without an enabled collider is rejected"))
    {
        return false;
    }
    body->set_enabled(false);
    if (!expect(fixture.advance(30) && agent->state() == NavigationAgentStatus::moving,
                "disabled physical components do not own the pose"))
    {
        return false;
    }
    collider->set_enabled(true);
    if (!expect(fixture.advance(1) && agent->state() == NavigationAgentStatus::invalid,
                "an enabled collider with a disabled rigid body is rejected"))
    {
        return false;
    }
    body->set_enabled(true);
    auto shape = collider->settings();
    shape.is_trigger = true;
    return expect(collider->set_settings(shape) && fixture.advance(30) &&
                      agent->state() == NavigationAgentStatus::moving,
                  "kinematic sensors are supported without introducing a second position solver");
}

} // namespace

int main()
{
    return requests_and_lifetime() && many_agents() && capacity_replacement() &&
                   physics_shape_ownership()
               ? 0
               : 1;
}
