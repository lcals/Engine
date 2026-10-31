#include "plugins/runtime/character_controller/character_camera.hpp"
#include "plugins/runtime/character_controller/character_camera_system.hpp"
#include "plugins/runtime/character_controller/character_controller.hpp"
#include "plugins/runtime/character_controller/character_controller_system.hpp"
#include "plugins/runtime/character_controller/character_visual.hpp"
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

using engine::plugins::character_controller::CharacterCamera;
using engine::plugins::character_controller::CharacterCameraSystem;
using engine::plugins::character_controller::CharacterController;
using engine::plugins::character_controller::CharacterControllerSystem;
using engine::plugins::character_controller::CharacterInput;
using engine::plugins::character_controller::CharacterVisual;

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
    CharacterControllerSystem controller{world};
    CharacterCameraSystem cameras{world, physics};
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
            !controller.initialize(config) || !cameras.initialize(config) ||
            world.create_scene("Character") == nullptr ||
            box("Floor", {0, -0.5, 0}, {30, 0.5, 30}) == nullptr)
        {
            return false;
        }
        player = world.scene()->create_node("Player");
        camera = world.scene()->create_node("Camera");
        if (player == nullptr || camera == nullptr ||
            player->add_component<engine::CharacterMotor>() == nullptr ||
            player->add_component<CharacterController>() == nullptr ||
            camera->add_component<engine::Camera>() == nullptr ||
            camera->add_component<CharacterCamera>() == nullptr)
        {
            return false;
        }
        auto* component = player->get_component<CharacterController>();
        auto settings = component->settings();
        settings.camera_node_id = camera->info().id;
        auto* rig = camera->get_component<CharacterCamera>();
        auto camera_settings = rig->settings();
        camera_settings.target_node_id = player->info().id;
        return rig->set_settings(camera_settings) && component->set_settings(settings) &&
               world.scene()->set_active_camera(camera->get_component<engine::Camera>()) &&
               physics.tick(0.0) && advance({}, 4);
    }

    bool advance(const CharacterInput& input, unsigned int frames)
    {
        for (unsigned int index = 0; index < frames; ++index)
        {
            if (!controller.set_input(input) || !controller.tick(1.0 / 60.0) ||
                !physics.tick(1.0 / 60.0) || !cameras.tick(0.0))
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
    auto camera_settings = fixture.camera->get_component<CharacterCamera>()->settings();
    camera_settings.pitch_min = 90;
    CharacterInput bad;
    bad.look = {std::numeric_limits<double>::quiet_NaN(), 0};
    if (!expect(
            !motor->set_settings(invalid) &&
                !fixture.camera->get_component<CharacterCamera>()->set_settings(camera_settings) &&
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
    auto* component = fixture.camera->get_component<CharacterCamera>();
    const auto foot = position(*fixture.player);
    auto camera = position(*fixture.camera);
    if (!expect(std::abs(camera.z() - foot.z() - 4) < 0.01 &&
                    std::abs(camera.y() - foot.y() - 1.6) < 0.01,
                "third person camera follows eye pivot at configured distance"))
    {
        return false;
    }
    auto* wall = fixture.box("CameraWall", {0, 2, 2}, {4, 2, 0.2});
    if (!wall || !fixture.physics.tick(0.0) || !fixture.cameras.tick(0.0))
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
        !fixture.cameras.tick(0.0) ||
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
    settings.target_node_id = 999999;
    return expect(component->set_settings(settings) && !fixture.cameras.tick(0.0),
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
    auto* scene = fixture.world.scene();
    auto* avatar = scene->create_node("Avatar");
    auto* head = scene->create_node("Head");
    auto* hands = scene->create_node("First person hands");
    auto* spectator = scene->create_node("Spectator");
    if (!avatar || !head || !hands || !spectator || !avatar->set_parent(fixture.player) ||
        !head->set_parent(avatar) || !hands->set_parent(avatar))
    {
        return false;
    }
    auto* body_mesh = avatar->add_component<engine::MeshRenderer>();
    auto* head_mesh = head->add_component<engine::MeshRenderer>();
    auto* hand_mesh = hands->add_component<engine::MeshRenderer>();
    auto* head_visual = head->add_component<CharacterVisual>();
    auto* hand_visual = hands->add_component<CharacterVisual>();
    auto* observer = spectator->add_component<engine::Camera>();
    if (!body_mesh || !head_mesh || !hand_mesh || !head_visual || !hand_visual || !observer)
    {
        return false;
    }
    auto visual = head_visual->settings();
    visual.kind = engine::schema::CharacterVisualKind::head;
    if (!head_visual->set_settings(visual))
    {
        return false;
    }
    visual.kind = engine::schema::CharacterVisualKind::first_person;
    auto* rig = fixture.camera->get_component<CharacterCamera>();
    auto* view = fixture.camera->get_component<engine::Camera>();
    auto settings = rig->settings();
    settings.mode = engine::schema::CharacterView::first_person;
    engine::schema::ComponentDataT before;
    engine::schema::ComponentDataT after;
    if (!hand_visual->set_settings(visual) || !body_mesh->build_data(before) ||
        !rig->set_settings(settings) || !fixture.cameras.tick(0.0) ||
        !expect(!view->mesh_visible(body_mesh->mesh_instance_id()) &&
                    !view->mesh_visible(head_mesh->mesh_instance_id()) &&
                    view->mesh_visible(hand_mesh->mesh_instance_id()) &&
                    observer->mesh_visible(body_mesh->mesh_instance_id()) &&
                    observer->mesh_visible(head_mesh->mesh_instance_id()) &&
                    !observer->mesh_visible(hand_mesh->mesh_instance_id()) &&
                    body_mesh->visible() && head_mesh->visible() && hand_mesh->visible() &&
                    body_mesh->build_data(after) && before == after,
                "first-person body/head/weapon filtering is per view and never mutates meshes"))
    {
        return false;
    }
    if (!avatar->remove_component(engine::MeshRenderer::component_type) ||
        (body_mesh = avatar->add_component<engine::MeshRenderer>()) == nullptr ||
        !fixture.cameras.tick(0.0) ||
        !expect(!view->mesh_visible(body_mesh->mesh_instance_id()),
                "replacement mesh receives its own per-view filter"))
    {
        return false;
    }
    settings.mode = engine::schema::CharacterView::third_person;
    if (!rig->set_settings(settings) || !fixture.cameras.tick(0.0) ||
        !expect(view->mesh_visible(body_mesh->mesh_instance_id()) &&
                    !view->mesh_visible(hand_mesh->mesh_instance_id()),
                "third-person camera shows body and hides first-person-only attachments"))
    {
        return false;
    }
    settings.mode = engine::schema::CharacterView::first_person;
    if (!rig->set_settings(settings) || !fixture.cameras.tick(0.0))
    {
        return false;
    }
    rig->set_enabled(false);
    if (!fixture.cameras.tick(0.0) || !expect(view->mesh_visible(body_mesh->mesh_instance_id()),
                                              "disabled rig clears its body filter"))
    {
        return false;
    }
    rig->set_enabled(true);
    if (!fixture.cameras.tick(0.0) ||
        !fixture.camera->remove_component(CharacterCamera::component_type) ||
        !fixture.cameras.tick(0.0) ||
        !expect(view->mesh_visible(body_mesh->mesh_instance_id()),
                "removed rig clears its body filter without a retained pointer"))
    {
        return false;
    }
    fixture.cameras.shutdown();
    return expect(observer->mesh_visible(hand_mesh->mesh_instance_id()) && body_mesh->visible(),
                  "camera system shutdown releases its transient filters");
}

bool player_selection_and_removal()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    CharacterInput move;
    move.move = {0, 1};
    auto* motor = fixture.player->get_component<engine::CharacterMotor>();
    if (!fixture.advance(move, 2) ||
        !fixture.player->remove_component(CharacterController::component_type))
    {
        return false;
    }
    const auto stopped = position(*fixture.player);
    if (!expect(fixture.advance(move, 2) &&
                    std::abs(position(*fixture.player).z() - stopped.z()) < 0.001,
                "removing controller clears old motor input on the next production tick"))
    {
        return false;
    }
    auto* component = fixture.player->add_component<CharacterController>();
    if (!component || !fixture.advance(move, 1))
    {
        return false;
    }
    component->set_enabled(false);
    const auto disabled = position(*fixture.player);
    if (!expect(fixture.advance(move, 1) &&
                    std::abs(position(*fixture.player).z() - disabled.z()) < 0.001,
                "disabled controller stops old active intent"))
    {
        return false;
    }
    component->set_enabled(true);
    auto* npc = fixture.world.scene()->create_node("NPC");
    auto* npc_motor = npc != nullptr ? npc->add_component<engine::CharacterMotor>() : nullptr;
    if (!npc_motor)
    {
        return false;
    }
    auto pose = npc->get_component<engine::Transform>()->settings();
    pose.position = {5, 0, 0};
    if (!npc->get_component<engine::Transform>()->set_settings(pose) ||
        !npc_motor->set_velocity({1, 0, 0}) || !fixture.advance(move, 3) ||
        !expect(position(*npc).x() > 5.03, "NPC motor operates without player input or camera"))
    {
        return false;
    }
    auto* second = npc->add_component<CharacterController>();
    if (!second || !fixture.controller.set_input(move) ||
        !expect(!fixture.controller.tick(1.0 / 60.0),
                "multiple automatic players fail rather than broadcasting input") ||
        !fixture.physics.tick(1.0 / 60.0) ||
        !expect(std::abs(motor->velocity().z()) < 0.001,
                "ambiguous selection also clears previously driven motor") ||
        !fixture.controller.set_player(fixture.player))
    {
        return false;
    }
    const auto npc_before = position(*npc);
    if (!fixture.advance(move, 3) ||
        !expect(position(*npc).x() > npc_before.x() &&
                    std::abs(position(*npc).z() - npc_before.z()) < 0.001,
                "explicit player leaves another controller's motor requests untouched") ||
        !fixture.controller.set_player(nullptr))
    {
        return false;
    }
    const auto deselected = position(*fixture.player);
    if (!fixture.advance(move, 3) ||
        !expect(std::abs(position(*fixture.player).z() - deselected.z()) < 0.001,
                "explicit null selection stays deselected"))
    {
        return false;
    }
    auto tuning = motor->settings();
    tuning.external_damping = 0;
    return expect(motor->set_settings(tuning) && motor->add_velocity({2, 0, 0}) &&
                      fixture.advance({}, 2) &&
                      position(*fixture.player).x() > deselected.x() + 0.05,
                  "clearing player input does not cancel independent knockback");
}

bool independent_follow_camera()
{
    Fixture fixture;
    if (!fixture.initialize() ||
        !fixture.player->remove_component(CharacterController::component_type) ||
        !fixture.player->remove_component(engine::CharacterMotor::component_type))
    {
        return false;
    }
    auto* parent = fixture.world.scene()->create_node("Moving parent");
    if (!parent || !fixture.player->set_parent(parent))
    {
        return false;
    }
    auto pose = parent->get_component<engine::Transform>()->settings();
    pose.position = {10, 5, 0};
    return expect(parent->get_component<engine::Transform>()->set_settings(pose) &&
                      fixture.physics.tick(0.0) && fixture.cameras.tick(0.0) &&
                      std::abs(position(*fixture.camera).x() - 10) < 0.01 &&
                      std::abs(position(*fixture.camera).y() - 6.6) < 0.05,
                  "independent camera follows target world pose without controller or motor");
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
                   player_selection_and_removal() && independent_follow_camera() &&
                   hierarchy_conflicts_and_cast_validation()
               ? 0
               : 1;
}
