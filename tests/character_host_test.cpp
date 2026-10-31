#include "runtime_plugins.hpp"

#include "plugins/runtime/character_controller/character_camera.hpp"
#include "plugins/runtime/character_controller/character_controller.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <array>
#include <cmath>
#include <cstdio>

namespace
{

using engine::plugins::character_controller::CharacterCamera;
using engine::plugins::character_controller::CharacterCameraSystem;
using engine::plugins::character_controller::CharacterController;
using engine::plugins::character_controller::CharacterControllerSystem;
using engine::plugins::character_controller::CharacterInput;

constexpr double delta_time = 1.0 / 60.0;

bool expect(bool value, const char* description)
{
    if (!value)
    {
        std::fprintf(stderr, "character host test: %s\n", description);
    }
    return value;
}

struct InstalledPlugins
{
    CharacterControllerSystem* controller = nullptr;
    CharacterCameraSystem* camera = nullptr;
    engine::PhysicsSystem* physics = nullptr;
    std::array<engine::RuntimeStage, 2> stages{};
    std::size_t calls = 0;
    bool reject_after_physics = false;
};

// RuntimeSetup 仅在 initialize 内同步调用；本借用不进入逐帧调度。
InstalledPlugins* installing = nullptr;

bool install_plugins(engine::RuntimeStage stage, engine::WorldSystem& world,
                     engine::WindowSystem& window, engine::PhysicsSystem& physics)
{
    if (installing == nullptr || installing->calls >= installing->stages.size() ||
        world.is_initialized() || physics.is_initialized())
    {
        return false;
    }
    installing->stages[installing->calls++] = stage;
    if (stage == engine::RuntimeStage::after_physics && installing->reject_after_physics)
    {
        return false;
    }
    const auto index = world.subsystem_count();
    if (!configure_runtime_plugins(stage, world, window, physics) ||
        world.subsystem_count() != index + 1)
    {
        return false;
    }
    if (stage == engine::RuntimeStage::before_physics)
    {
        installing->controller = static_cast<CharacterControllerSystem*>(world.subsystem(index));
    }
    else
    {
        installing->camera = static_cast<CharacterCameraSystem*>(world.subsystem(index));
    }
    installing->physics = &physics;
    return true;
}

bool tick_one(engine::RuntimeHost& host)
{
    const auto* scene = host.world()->scene();
    for (unsigned int attempt = 0; attempt < 120; ++attempt)
    {
        const auto status = host.tick(delta_time);
        if (status == engine::FrameStatus::running)
        {
            return expect(host.world()->scene() == scene,
                          "a running frame must not discard the test scene after render failure");
        }
        if (status != engine::FrameStatus::paused)
        {
            return false;
        }
        SDL_Delay(5);
    }
    return false;
}

engine::Vec3 position(const engine::Node& node)
{
    return node.get_component<engine::Transform>()->settings().position;
}

bool same_horizontal_position(const engine::Vec3& first, const engine::Vec3& second)
{
    return std::hypot(first.x() - second.x(), first.z() - second.z()) < 0.0001;
}

bool host_input_physics_camera_order()
{
    engine::RuntimeHost host;
    InstalledPlugins plugins;
    engine::EngineConfig config;
    config.window_title = "Character host ordering";
    config.width = 320;
    config.height = 240;

    // 拒绝第二个装配位置后允许同一宿主重新装配，旧借用不再使用。
    plugins.reject_after_physics = true;
    installing = &plugins;
    const bool rejected = !host.initialize(config, nullptr, nullptr, install_plugins);
    installing = nullptr;
    if (!expect(rejected && host.world() == nullptr && host.window() == nullptr,
                "failed staged setup releases the partial host"))
    {
        return false;
    }
    plugins = {};
    installing = &plugins;
    const bool initialized = host.initialize(config, nullptr, nullptr, install_plugins);
    installing = nullptr;
    if (!expect(initialized && plugins.calls == 2 &&
                    plugins.stages[0] == engine::RuntimeStage::before_physics &&
                    plugins.stages[1] == engine::RuntimeStage::after_physics &&
                    plugins.controller != nullptr && plugins.controller->is_initialized() &&
                    plugins.camera != nullptr && plugins.camera->is_initialized() &&
                    plugins.physics != nullptr && plugins.physics->is_initialized(),
                "real runtime host initializes production plugins in the two setup positions"))
    {
        return false;
    }
    auto* scene = host.world()->create_scene("Character host");
    if (scene == nullptr)
    {
        return false;
    }
    auto* floor = scene->create_node("Floor");
    auto* player = scene->create_node("Player");
    auto* camera_node = scene->create_node("Camera");
    if (floor == nullptr || player == nullptr || camera_node == nullptr)
    {
        return false;
    }
    auto floor_pose = floor->get_component<engine::Transform>()->settings();
    floor_pose.position = {0, -0.5, 0};
    auto* collider = floor->add_component<engine::Collider>();
    auto* motor = player->add_component<engine::CharacterMotor>();
    auto* controller = player->add_component<CharacterController>();
    auto* camera = camera_node->add_component<engine::Camera>();
    auto* follow = camera_node->add_component<CharacterCamera>();
    if (collider == nullptr || motor == nullptr || controller == nullptr || camera == nullptr ||
        follow == nullptr || !floor->get_component<engine::Transform>()->set_settings(floor_pose))
    {
        return false;
    }
    auto collision = collider->settings();
    collision.half_extents = {20, 0.5, 20};
    auto control = controller->settings();
    control.camera_node_id = camera_node->info().id;
    auto camera_settings = follow->settings();
    camera_settings.target_node_id = player->info().id;
    camera_settings.mode = engine::schema::CharacterView::third_person;
    camera_settings.eye_height = 1.6F;
    camera_settings.camera_distance = 4.0F;
    if (!collider->set_settings(collision) || !controller->set_settings(control) ||
        !follow->set_settings(camera_settings) || !scene->set_active_camera(camera) ||
        !plugins.controller->set_player(player) || !tick_one(host) || !tick_one(host))
    {
        return false;
    }

    const auto before = position(*player);
    CharacterInput forward;
    forward.move = {0, 1};
    if (!plugins.controller->set_input(forward) || !tick_one(host))
    {
        return false;
    }
    const auto moved = position(*player);
    const auto camera_position = position(*camera_node);
    if (!expect(std::abs(moved.z() - before.z() + control.move_speed * delta_time) < 0.01,
                "one host tick consumes player intent in the same physics step") ||
        !expect(std::abs(camera_position.z() - moved.z() - camera_settings.camera_distance) <
                        0.001 &&
                    std::abs(camera_position.y() - moved.y() - camera_settings.eye_height) < 0.001,
                "camera follows the final same-frame physical position without a frame of lag"))
    {
        return false;
    }

    controller->set_enabled(false);
    if (!plugins.controller->set_input(forward) || !tick_one(host) ||
        !expect(same_horizontal_position(moved, position(*player)),
                "disabling the selected controller clears movement before physics"))
    {
        return false;
    }
    controller->set_enabled(true);
    if (!plugins.controller->set_player(player) || !plugins.controller->set_input(forward) ||
        !tick_one(host) ||
        !expect(position(*player).z() < moved.z() - 0.02,
                "reenabled explicit player selection can move again"))
    {
        return false;
    }
    const auto before_removal = position(*player);
    if (!player->remove_component(CharacterController::component_type) ||
        !plugins.controller->set_input(forward) || !tick_one(host) ||
        !expect(same_horizontal_position(before_removal, position(*player)),
                "removing the controller cannot leave its prior velocity active") ||
        !expect(plugins.calls == 2, "setup callbacks are not part of frame ticking"))
    {
        return false;
    }
    host.shutdown();
    host.shutdown();
    return expect(host.world() == nullptr && host.renderer() == nullptr,
                  "host owns production plugin teardown and supports repeated shutdown");
}

} // namespace

int main(int, char**)
{
    return host_input_physics_camera_order() ? 0 : 1;
}
