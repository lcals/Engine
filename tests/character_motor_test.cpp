#include "runtime/engine_config.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{

constexpr double step = 1.0 / 60.0;

bool expect(bool value, const char* description)
{
    if (!value)
    {
        std::fprintf(stderr, "character motor test: %s\n", description);
    }
    return value;
}

bool close(double value, double expected, double tolerance = 0.001)
{
    return std::abs(value - expected) < tolerance;
}

struct Fixture
{
    engine::WorldSystem world;
    engine::PhysicsSystem physics{&world};
    engine::Node* player = nullptr;

    engine::Node* box(const char* name, const engine::Vec3& position, const engine::Vec3& extents)
    {
        auto* node = world.scene()->create_node(name);
        auto* collider = node ? node->add_component<engine::Collider>() : nullptr;
        if (collider == nullptr)
        {
            return nullptr;
        }
        auto* transform = node->get_component<engine::Transform>();
        auto pose = transform->settings();
        pose.position = position;
        auto shape = collider->settings();
        shape.half_extents = extents;
        return transform->set_settings(pose) && collider->set_settings(shape) ? node : nullptr;
    }

    bool initialize(bool airborne = false)
    {
        const engine::EngineConfig config;
        if (!world.initialize(config) || !physics.initialize(config) ||
            world.create_scene("Character motor") == nullptr ||
            box("Floor", {0, -0.5, 0}, {30, 0.5, 30}) == nullptr)
        {
            return false;
        }
        player = world.scene()->create_node("Player");
        if (player == nullptr || player->add_component<engine::CharacterMotor>() == nullptr)
        {
            return false;
        }
        if (airborne)
        {
            auto* transform = player->get_component<engine::Transform>();
            auto pose = transform->settings();
            pose.position = {0, 3, 0};
            auto settings = motor().settings();
            settings.gravity_factor = 0;
            settings.external_damping = 0;
            return transform->set_settings(pose) && motor().set_settings(settings);
        }
        return advance(4) && motor().grounded();
    }

    engine::CharacterMotor& motor()
    {
        return *player->get_component<engine::CharacterMotor>();
    }

    engine::Vec3 position() const
    {
        return player->get_component<engine::Transform>()->settings().position;
    }

    bool advance(unsigned int count)
    {
        for (unsigned int index = 0; index < count; ++index)
        {
            if (!physics.tick(step))
            {
                return false;
            }
        }
        return true;
    }
};

bool queued_velocity_and_fixed_steps()
{
    Fixture fixture;
    if (!expect(fixture.initialize(true), "initialize an airborne motor before synchronization"))
    {
        return false;
    }
    auto& motor = fixture.motor();
    engine::schema::ComponentDataT before;
    engine::schema::ComponentDataT after;
    if (!expect(motor.build_data(before) && motor.add_velocity({2, 0, 1}) &&
                    motor.add_velocity({4, 0, -1}) && motor.set_velocity({0, 0, 0}),
                "accumulate velocity deltas separately from active input") ||
        !expect(!motor.add_velocity({9998, 0, 0}) &&
                    !motor.add_velocity({0, std::numeric_limits<double>::infinity(), 0}) &&
                    !motor.add_velocity({0, 0, std::numeric_limits<double>::quiet_NaN()}) &&
                    !motor.set_velocity({0, 1, 0}),
                "invalid or overflowing requests preserve the accepted delta") ||
        !expect(motor.build_data(after) && before == after,
                "queued movement does not enter the scene snapshot") ||
        !expect(fixture.physics.tick(0) && close(fixture.position().x(), 0) &&
                    close(motor.velocity().x(), 0),
                "initial synchronization preserves requests without applying a velocity") ||
        !expect(fixture.physics.tick(step / 2) && close(fixture.position().x(), 0),
                "a frame without a full fixed step retains the accumulated delta") ||
        !expect(fixture.physics.tick(step / 2) && close(fixture.position().x(), 0.1) &&
                    close(motor.velocity().x(), 6) && close(motor.velocity().z(), 0),
                "the first fixed step applies the original accumulated delta exactly once") ||
        !expect(motor.set_velocity({0, 0, 0}) && fixture.physics.tick(step * 3) &&
                    close(fixture.position().x(), 0.4) && close(motor.velocity().x(), 6),
                "multiple fixed substeps retain velocity without consuming the delta again"))
    {
        return false;
    }
    return true;
}

bool input_and_damping()
{
    Fixture fixture;
    if (!expect(fixture.initialize(), "settle a motor on the floor"))
    {
        return false;
    }
    auto& motor = fixture.motor();
    if (!expect(close(motor.settings().external_damping, 6) && motor.jump(5) &&
                    motor.set_velocity({0, 0, 3}) && motor.add_velocity({6, 0, 0}),
                "queue active movement, jump and independent knockback"))
    {
        return false;
    }
    motor.clear_input();
    if (!expect(fixture.advance(1) && motor.grounded() && close(motor.velocity().x(), 6) &&
                    close(motor.velocity().z(), 0),
                "clear_input cancels active movement and jump but preserves pending knockback") ||
        !expect(motor.set_velocity({0, 0, 2}) && fixture.advance(1) &&
                    close(motor.velocity().x(), 6 * std::exp(-0.1)) &&
                    close(motor.velocity().z(), 2),
                "active movement combines with independently damped external velocity"))
    {
        return false;
    }
    const auto before = fixture.position();
    double previous = motor.velocity().x();
    for (unsigned int index = 0; index < 60; ++index)
    {
        motor.clear_input();
        if (!expect(motor.set_velocity({0, 0, 0}) && fixture.advance(1) &&
                        motor.velocity().x() > 0 && motor.velocity().x() < previous &&
                        close(motor.velocity().z(), 0),
                    "empty input never erases knockback and each fixed step decays its speed"))
        {
            return false;
        }
        previous = motor.velocity().x();
    }
    auto invalid = motor.settings();
    invalid.external_damping = -1;
    if (!expect(!motor.set_settings(invalid), "negative damping is rejected atomically"))
    {
        return false;
    }
    invalid.external_damping = std::numeric_limits<float>::quiet_NaN();
    return expect(!motor.set_settings(invalid) && close(motor.settings().external_damping, 6) &&
                      motor.velocity().x() < 0.05 && fixture.position().x() > before.x() + 0.5,
                  "damping settles the impulse over time and invalid settings retain the default");
}

bool knockback_collisions()
{
    Fixture fixture;
    if (!fixture.initialize() || fixture.box("Wall", {0, 3, -3}, {10, 3, 0.25}) == nullptr)
    {
        return false;
    }
    auto& motor = fixture.motor();
    auto settings = motor.settings();
    settings.external_damping = 0;
    const double ground = fixture.position().y();
    if (!expect(motor.set_settings(settings) && fixture.physics.tick(0) &&
                    motor.add_velocity({2, 5, -7}) && fixture.advance(15) && !motor.grounded() &&
                    fixture.position().y() > ground + 0.7,
                "vertical knockback enters the normal airborne gravity arc") ||
        !expect(fixture.advance(105) && motor.grounded() &&
                    std::abs(fixture.position().y() - ground) < 0.05 &&
                    fixture.position().z() > -2.5 && fixture.position().z() < -2.3 &&
                    fixture.position().x() > 3.5 && close(motor.velocity().z(), 0, 0.05),
                "knockback slides along a solid wall and lands without stored blocked velocity"))
    {
        return false;
    }
    return true;
}

bool live_settings_keep_velocity()
{
    Fixture fixture;
    if (!fixture.initialize(true) || !fixture.motor().add_velocity({6, 3, 0}) ||
        !fixture.advance(1))
    {
        return false;
    }
    auto& motor = fixture.motor();
    auto settings = motor.settings();
    settings.external_damping = 6;
    settings.gravity_factor = 2;
    settings.step_height = 0.5F;
    settings.ground_snap_distance = 0.25F;
    if (!expect(motor.set_settings(settings) && fixture.physics.tick(0) &&
                    close(motor.velocity().x(), 6) && close(motor.velocity().y(), 3),
                "live movement settings preserve the current Jolt and external velocity") ||
        !expect(fixture.advance(1) && close(motor.velocity().x(), 6) && motor.velocity().y() > 0 &&
                    motor.velocity().y() < 3,
                "new gravity applies without resetting existing vertical speed"))
    {
        return false;
    }
    return expect(fixture.advance(1) && close(motor.velocity().x(), 6 * std::exp(-0.1)),
                  "new damping decays the existing external velocity without rebuilding it");
}

bool ceiling_contact()
{
    Fixture fixture;
    if (!fixture.initialize() || fixture.box("Ceiling", {0, 2.5, 0}, {3, 0.1, 3}) == nullptr)
    {
        return false;
    }
    auto& motor = fixture.motor();
    if (!motor.add_velocity({0, 8, 0}))
    {
        return false;
    }
    double peak = 0;
    for (unsigned int index = 0; index < 30; ++index)
    {
        if (!fixture.advance(1))
        {
            return false;
        }
        peak = std::max(peak, fixture.position().y());
    }
    return expect(peak > 0.4 && peak < 0.65 && fixture.position().y() < 0.3 &&
                      fixture.advance(60) && motor.grounded(),
                  "ceiling collision removes upward speed so gravity resumes without hovering");
}

bool restore_disable_and_rebuild()
{
    Fixture fixture;
    if (!fixture.initialize(true) || !fixture.physics.tick(0))
    {
        return false;
    }
    auto& motor = fixture.motor();
    const auto first_id = motor.instance_id();
    engine::schema::ComponentDataT saved;
    if (!expect(first_id != 0 && motor.build_data(saved) && motor.add_velocity({4, 3, 0}) &&
                    fixture.advance(1),
                "start persistent external movement on one component identity"))
    {
        return false;
    }
    auto invalid = saved;
    invalid.payload.AsCharacterMotorData()->radius = -1;
    if (!expect(!motor.restore_data(invalid) && fixture.advance(1) &&
                    close(motor.velocity().x(), 4) && close(motor.velocity().y(), 3),
                "failed restore preserves existing external and vertical speed") ||
        !expect(motor.add_velocity({3, 2, 1}) && motor.restore_data(saved) &&
                    fixture.physics.tick(0) && close(motor.velocity().x(), 0) &&
                    close(motor.velocity().y(), 0),
                "successful restore clears queued deltas and resets the existing Jolt instance"))
    {
        return false;
    }
    const auto restored = fixture.position();
    if (!expect(fixture.advance(2) && fixture.position() == restored,
                "restore does not revive motion from the old physics state") ||
        !expect(motor.add_velocity({4, 2, 0}) && fixture.advance(1) &&
                    motor.add_velocity({1, 1, 0}),
                "prepare current and queued movement for disable"))
    {
        return false;
    }
    motor.set_enabled(false);
    if (!expect(!motor.add_velocity({1, 0, 0}) && fixture.physics.tick(0) &&
                    fixture.physics.character_count() == 0,
                "disabled motor rejects input and discards its Jolt state"))
    {
        return false;
    }
    motor.set_enabled(true);
    const auto disabled = fixture.position();
    if (!expect(fixture.advance(2) && fixture.position() == disabled &&
                    motor.instance_id() == first_id,
                "reenabling the same component cannot restore old velocity") ||
        !expect(motor.add_velocity({5, 0, 0}) && fixture.advance(1),
                "restart motion before replacing the component") ||
        !expect(fixture.player->remove_component(engine::CharacterMotor::component_type),
                "remove the moving component"))
    {
        return false;
    }
    auto* replacement = fixture.player->add_component<engine::CharacterMotor>();
    if (!expect(replacement != nullptr && replacement->instance_id() != first_id &&
                    replacement->set_settings(*saved.payload.AsCharacterMotorData()) &&
                    replacement->add_velocity({2, 0, 0}) && fixture.advance(1) &&
                    close(replacement->velocity().x(), 2),
                "new instance uses only its own pending delta even on the same node"))
    {
        return false;
    }
    auto shape = replacement->settings();
    shape.radius = 0.35F;
    const auto before_rebuild = fixture.position();
    if (!expect(replacement->set_settings(shape) && fixture.advance(1) &&
                    fixture.position() == before_rebuild,
                "rebuilding the physical capsule discards external velocity"))
    {
        return false;
    }
    fixture.physics.clear();
    fixture.physics.clear();
    return expect(fixture.physics.character_count() == 0 && fixture.advance(1) &&
                      fixture.position() == before_rebuild,
                  "repeated clear releases physics and pending motion without resurrection");
}

} // namespace

int main()
{
    return queued_velocity_and_fixed_steps() && input_and_damping() &&
                   live_settings_keep_velocity() && knockback_collisions() && ceiling_contact() &&
                   restore_disable_and_rebuild()
               ? 0
               : 1;
}
