#include "plugins/runtime/character_controller/character_controller.hpp"
#include "plugins/runtime/character_controller/character_controller_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{

using engine::plugins::character_controller::CharacterController;
using engine::plugins::character_controller::CharacterControllerSystem;
using engine::plugins::character_controller::CharacterInput;

bool expect(bool value, const char* description)
{
    if (!value)
    {
        std::fprintf(stderr, "character controller test: %s\n", description);
    }
    return value;
}

struct Fixture
{
    engine::WorldSystem world;
    engine::PhysicsSystem physics{&world};
    CharacterControllerSystem controller{world, physics};
    engine::Node* player = nullptr;
    engine::Node* camera = nullptr;

    engine::Node* box(const char* name, const engine::Vec3& position, const engine::Vec3& extents)
    {
        auto* node = world.scene()->create_node(name);
        if (node == nullptr)
        {
            return nullptr;
        }
        auto* transform = node->get_component<engine::Transform>();
        auto pose = transform->settings();
        pose.position = position;
        auto* collider = node->add_component<engine::Collider>();
        if (collider == nullptr || !transform->set_settings(pose))
        {
            return nullptr;
        }
        auto settings = collider->settings();
        settings.half_extents = extents;
        return collider->set_settings(settings) ? node : nullptr;
    }

    bool initialize()
    {
        const engine::EngineConfig config;
        if (!world.initialize(config) || !physics.initialize(config) ||
            !controller.initialize(config) || world.create_scene("Character") == nullptr ||
            box("Floor", {0, -0.5, 0}, {30, 0.5, 30}) == nullptr)
        {
            return false;
        }
        player = world.scene()->create_node("Player");
        camera = world.scene()->create_node("Camera");
        if (player == nullptr || camera == nullptr ||
            player->add_component<engine::CharacterMotor>() == nullptr ||
            player->add_component<CharacterController>() == nullptr ||
            camera->add_component<engine::Camera>() == nullptr)
        {
            return false;
        }
        auto* component = player->get_component<CharacterController>();
        auto settings = component->settings();
        settings.camera_node_id = camera->info().id;
        return component->set_settings(settings) &&
               world.scene()->set_active_camera(camera->get_component<engine::Camera>()) &&
               physics.tick(0.0) && advance({}, 4);
    }

    bool advance(const CharacterInput& input, unsigned int frames)
    {
        for (unsigned int index = 0; index < frames; ++index)
        {
            if (!controller.set_input(input) || !controller.tick(1.0 / 60.0) ||
                !physics.tick(1.0 / 60.0) || !controller.update_cameras())
            {
                return false;
            }
        }
        return true;
    }
};

engine::Vec3 position(const engine::Node& node)
{
    return node.get_component<engine::Transform>()->settings().position;
}

bool movement_jump_and_settings()
{
    Fixture fixture;
    if (!expect(fixture.initialize(), "initialize schema-backed player and real Jolt world"))
    {
        return false;
    }
    auto* motor = fixture.player->get_component<engine::CharacterMotor>();
    auto* controller = fixture.player->get_component<CharacterController>();
    engine::schema::ComponentDataT before;
    engine::schema::ComponentDataT after;
    if (!expect(motor->grounded() && fixture.physics.character_count() == 1,
                "foot-origin capsule stands on floor") ||
        !expect(motor->build_data(before) && motor->set_velocity({1, 0, 2}) &&
                    motor->build_data(after) && before == after,
                "input and contact state do not enter persistent motor settings") ||
        !expect(!motor->set_velocity({0, 1, 0}),
                "vertical velocity cannot bypass gravity and jump") ||
        !expect(!motor->set_velocity({std::numeric_limits<double>::infinity(), 0, 0}),
                "invalid velocity is rejected"))
    {
        return false;
    }
    auto invalid = motor->settings();
    invalid.radius = -1;
    auto camera_settings = controller->settings();
    camera_settings.pitch_min = 90;
    CharacterInput bad;
    bad.look = {std::numeric_limits<double>::quiet_NaN(), 0};
    if (!expect(!motor->set_settings(invalid) && !controller->set_settings(camera_settings) &&
                    !fixture.controller.set_input(bad),
                "invalid component settings and input preserve valid state"))
    {
        return false;
    }
    CharacterInput forward;
    forward.move = {0, 1};
    const auto start = position(*fixture.player);
    if (!expect(fixture.advance(forward, 30), "walk forward through real simulation") ||
        !expect(std::abs(position(*fixture.player).z() - start.z() + 2.0) < 0.1,
                "forward follows camera negative Z at configured speed") ||
        !expect(fixture.controller.tick(1.0 / 60.0) && fixture.physics.tick(1.0 / 60.0) &&
                    std::abs(motor->velocity().z()) < 0.01,
                "injected input is consumed once instead of sticking forever"))
    {
        return false;
    }
    CharacterInput diagonal;
    diagonal.move = {1, 1};
    const auto diagonal_start = position(*fixture.player);
    if (!expect(fixture.advance(diagonal, 30), "diagonal movement") ||
        !expect(std::abs(std::hypot(position(*fixture.player).x() - diagonal_start.x(),
                                    position(*fixture.player).z() - diagonal_start.z()) -
                         2.0) < 0.1,
                "diagonal input does not run faster"))
    {
        return false;
    }
    CharacterInput jump;
    jump.jump = true;
    const double ground = position(*fixture.player).y();
    if (!expect(fixture.advance(jump, 1) && !motor->grounded(), "jump leaves ground") ||
        !expect(!motor->jump(5.0F), "airborne character cannot jump again") ||
        !expect(fixture.advance({}, 15) && position(*fixture.player).y() > ground + 0.7,
                "jump speed produces a physical arc") ||
        !expect(fixture.advance({}, 100) && motor->grounded() &&
                    std::abs(position(*fixture.player).y() - ground) < 0.05,
                "gravity lands on floor and restores grounded state"))
    {
        return false;
    }
    const auto saved = controller->settings();
    if (!controller->build_data(before) || !controller->restore_data(before) ||
        !expect(controller->settings() == saved, "controller settings restore without input state"))
    {
        return false;
    }
    return true;
}

bool collisions_steps_and_lifecycle()
{
    Fixture fixture;
    if (!fixture.initialize() || fixture.box("Step", {0, 0.125, -2}, {2, 0.125, 0.75}) == nullptr ||
        fixture.box("Wall", {0, 2, -4}, {4, 2, 0.25}) == nullptr)
    {
        return false;
    }
    CharacterInput forward;
    forward.move = {0, 1};
    double maximum_height = 0.0;
    for (unsigned int frame = 0; frame < 100; ++frame)
    {
        if (!fixture.advance(forward, 1))
        {
            return false;
        }
        maximum_height = std::max(maximum_height, position(*fixture.player).y());
    }
    if (!expect(maximum_height > 0.20, "Jolt ExtendedUpdate climbs a configured low step") ||
        !expect(position(*fixture.player).z() > -3.48 && position(*fixture.player).z() < -3.1,
                "capsule stops before solid wall instead of passing through"))
    {
        return false;
    }
    const double old_x = position(*fixture.player).x();
    forward.move = {1, 1};
    if (!expect(fixture.advance(forward, 20) && position(*fixture.player).x() > old_x + 0.7 &&
                    position(*fixture.player).z() > -3.48,
                "blocked forward movement slides along wall"))
    {
        return false;
    }
    auto* motor = fixture.player->get_component<engine::CharacterMotor>();
    motor->set_enabled(false);
    fixture.player->get_component<CharacterController>()->set_enabled(false);
    if (!expect(fixture.physics.tick(0.0) && fixture.physics.character_count() == 0 &&
                    !motor->grounded(),
                "disabled component releases virtual character and ground state"))
    {
        return false;
    }
    motor->set_enabled(true);
    if (!expect(fixture.physics.tick(0.0) && fixture.physics.character_count() == 1,
                "reenabling component recreates virtual character") ||
        !expect(fixture.player->remove_component(engine::CharacterMotor::component_type) &&
                    fixture.player->add_component<engine::CharacterMotor>() != nullptr &&
                    fixture.physics.tick(0.0) && fixture.physics.character_count() == 1,
                "replacement component cannot reuse stale per-instance physics state"))
    {
        return false;
    }
    const auto id = fixture.player->id();
    if (!expect(fixture.world.scene()->destroy_node(id) && fixture.physics.tick(0.0) &&
                    fixture.physics.character_count() == 0,
                "deleting node removes character without borrowed pointer lifetime"))
    {
        return false;
    }
    fixture.world.destroy_scene();
    return expect(fixture.physics.tick(0.0) && fixture.controller.tick(0.0) &&
                      fixture.physics.body_count() == 0,
                  "scene shutdown releases collision world and controller state");
}

bool camera_modes_and_occlusion()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto* component = fixture.player->get_component<CharacterController>();
    const auto foot = position(*fixture.player);
    auto camera = position(*fixture.camera);
    if (!expect(std::abs(camera.z() - foot.z() - 4) < 0.01 &&
                    std::abs(camera.y() - foot.y() - 1.6) < 0.01,
                "third person camera follows eye pivot at configured distance"))
    {
        return false;
    }
    auto* wall = fixture.box("CameraWall", {0, 2, 2}, {4, 2, 0.2});
    if (!wall || !fixture.physics.tick(0.0) || !fixture.controller.update_cameras())
    {
        return false;
    }
    camera = position(*fixture.camera);
    if (!expect(camera.z() > 1.5 && camera.z() < 1.7,
                "sphere sweep pulls third person camera ahead of blocking wall"))
    {
        return false;
    }
    auto* collider = wall->get_component<engine::Collider>();
    auto collision = collider->settings();
    collision.is_trigger = true;
    if (!collider->set_settings(collision) || !fixture.physics.tick(0.0) ||
        !fixture.controller.update_cameras() ||
        !expect(std::abs(position(*fixture.camera).z() - 4) < 0.01,
                "trigger volumes do not block camera"))
    {
        return false;
    }
    CharacterInput toggle;
    toggle.toggle_view = true;
    if (!expect(fixture.advance(toggle, 1) &&
                    component->settings().mode == engine::schema::CharacterView::first_person &&
                    std::abs(position(*fixture.camera).z() - position(*fixture.player).z()) < 0.01,
                "same component switches to first person eye position"))
    {
        return false;
    }
    CharacterInput look;
    look.look = {750, -2000};
    if (!fixture.advance(look, 1))
    {
        return false;
    }
    const auto camera_rotation =
        fixture.camera->get_component<engine::Transform>()->settings().rotation;
    if (!expect(std::abs(camera_rotation.y() + 90) < 0.01 &&
                    std::abs(camera_rotation.x() - component->settings().pitch_max) < 0.01,
                "mouse look rotates yaw and clamps pitch"))
    {
        return false;
    }
    CharacterInput forward;
    forward.move = {0, 1};
    const auto start = position(*fixture.player);
    if (!expect(fixture.advance(forward, 15) && position(*fixture.player).x() > start.x() + 0.9 &&
                    std::abs(position(*fixture.player).z() - start.z()) < 0.05,
                "movement direction follows view yaw while ignoring camera pitch"))
    {
        return false;
    }
    auto settings = component->settings();
    settings.camera_node_id = 999999;
    return expect(component->set_settings(settings) && !fixture.controller.update_cameras(),
                  "missing explicit camera reference fails instead of selecting another camera");
}

bool first_person_visibility_lifecycle()
{
    engine::ResourceSystem resources;
    if (!resources.initialize({}))
    {
        return false;
    }
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto* avatar = fixture.world.scene()->create_node("Avatar");
    auto* hidden = fixture.world.scene()->create_node("Already hidden");
    auto* outside = fixture.world.scene()->create_node("World mesh");
    if (avatar == nullptr || hidden == nullptr || outside == nullptr ||
        !avatar->set_parent(fixture.player) || !hidden->set_parent(avatar))
    {
        return false;
    }
    auto* renderer = avatar->add_component<engine::MeshRenderer>();
    auto* hidden_renderer = hidden->add_component<engine::MeshRenderer>();
    auto* outside_renderer = outside->add_component<engine::MeshRenderer>();
    if (renderer == nullptr || hidden_renderer == nullptr || outside_renderer == nullptr)
    {
        return false;
    }
    hidden_renderer->set_visible(false);
    engine::schema::ComponentDataT before;
    engine::schema::ComponentDataT after;
    auto* controller = fixture.player->get_component<CharacterController>();
    auto settings = controller->settings();
    settings.mode = engine::schema::CharacterView::first_person;
    if (!renderer->build_data(before) || !controller->set_settings(settings) ||
        !fixture.controller.update_cameras() ||
        !expect(
            !renderer->visible() && renderer->enabled() && !hidden_renderer->visible() &&
                outside_renderer->visible() && renderer->build_data(after) && before == after,
            "first person hides only own subtree without disabling animation or changing snapshot"))
    {
        return false;
    }
    if (!avatar->remove_component(engine::MeshRenderer::component_type) ||
        (renderer = avatar->add_component<engine::MeshRenderer>()) == nullptr ||
        !fixture.controller.update_cameras() ||
        !expect(!renderer->visible(),
                "replacement renderer is hidden with its new instance identity"))
    {
        return false;
    }
    settings.mode = engine::schema::CharacterView::third_person;
    if (!controller->set_settings(settings) || !fixture.controller.update_cameras() ||
        !expect(renderer->visible() && !hidden_renderer->visible(),
                "third person restores original per-renderer visibility"))
    {
        return false;
    }
    settings.mode = engine::schema::CharacterView::first_person;
    if (!controller->set_settings(settings) || !fixture.controller.update_cameras())
    {
        return false;
    }
    controller->set_enabled(false);
    if (!fixture.controller.update_cameras() ||
        !expect(renderer->visible(), "disabling controller restores visibility"))
    {
        return false;
    }
    controller->set_enabled(true);
    if (!fixture.controller.update_cameras() ||
        !fixture.player->remove_component(CharacterController::component_type) ||
        !fixture.controller.update_cameras() ||
        !expect(renderer->visible(), "removing controller restores visibility"))
    {
        return false;
    }
    controller = fixture.player->add_component<CharacterController>();
    if (!controller || !controller->set_settings(settings) ||
        !fixture.controller.update_cameras() ||
        !expect(!renderer->visible(), "replacement controller can take visibility ownership"))
    {
        return false;
    }
    fixture.controller.shutdown();
    return expect(
        renderer->visible() && !hidden_renderer->visible(),
        "system shutdown restores transient changes while preserving preexisting hidden mesh");
}

bool hierarchy_conflicts_and_cast_validation()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    float fraction = 2.0F;
    if (!expect(!fixture.physics.cast_sphere({}, {}, -1, fraction) && fraction == 0,
                "invalid sphere query resets output") ||
        !expect(fixture.physics.cast_sphere({0, 4, 0}, {}, 0.1F, fraction) && fraction == 1,
                "zero displacement query succeeds in free space"))
    {
        return false;
    }
    auto* transform = fixture.player->get_component<engine::Transform>();
    auto pose = transform->settings();
    pose.scale = {2, 1, 1};
    if (!transform->set_settings(pose) ||
        !expect(!fixture.physics.tick(0.0) && fixture.physics.character_count() == 0,
                "unsupported character scaling fails with released physics state"))
    {
        return false;
    }
    pose.scale = {1, 1, 1};
    if (!transform->set_settings(pose) || !fixture.physics.tick(0.0) ||
        fixture.player->add_component<engine::Collider>() == nullptr)
    {
        return false;
    }
    return expect(!fixture.physics.tick(0.0),
                  "character and separate rigid collider cannot overlap ownership");
}

} // namespace

int main()
{
    return movement_jump_and_settings() && collisions_steps_and_lifecycle() &&
                   camera_modes_and_occlusion() && first_person_visibility_lifecycle() &&
                   hierarchy_conflicts_and_cast_validation()
               ? 0
               : 1;
}
