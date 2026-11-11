#include "control_fixture.hpp"
#include "runtime_plugins.hpp"
#include "runtime_settings_fixture.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cmath>
using namespace test_support;
using namespace engine;
namespace
{
camera::CameraTypeRegistry* camera_types = nullptr;
character::CharacterAbilityRegistry* abilities = nullptr;
bool reject_initialized = false;
int calls = 0;
bool install(RuntimeStage stage, WorldSystem& world, WindowSystem& window, PhysicsSystem& physics,
             NavigationSystem& navigation, RenderSystem& render, TaskSystem& tasks)
{
    ++calls;
    if (stage == RuntimeStage::initialized && reject_initialized)
    {
        return false;
    }
    return assemble_runtime_plugins(stage, world, window, physics, navigation, render, tasks,
                                    camera_types, abilities);
}
bool tick(RuntimeHost& host)
{
    for (int i = 0; i < 120; ++i)
    {
        const auto state = host.tick(1.0 / 60);
        if (state == FrameStatus::running)
        {
            return true;
        }
        if (state != FrameStatus::paused)
        {
            return false;
        }
        SDL_Delay(5);
    }
    return false;
}
bool run()
{
    camera::CameraTypeRegistry types;
    character::CharacterAbilityRegistry registry;
    if (!camera::register_builtin_camera_types(types) || !types.freeze() ||
        !character::register_builtin_character_abilities(registry) || !registry.freeze())
    {
        return false;
    }
    camera_types = &types;
    abilities = &registry;
    RuntimeSettings settings;
    if (!load_runtime_settings(settings))
    {
        return false;
    }
    auto config = settings.engine;
    config.width = 640;
    config.height = 360;
    config.window_title = "Camera and character module test";
    RuntimeHost host;
    reject_initialized = true;
    if (!check(!host.initialize(config, nullptr, nullptr, nullptr, install, nullptr,
                                settings.parallel) &&
                   host.world() == nullptr,
               "failed registry registration rolls back initialized host"))
    {
        return false;
    }
    calls = 0;
    reject_initialized = false;
    if (!check(host.initialize(config, nullptr, nullptr, nullptr, install, nullptr,
                               settings.parallel) &&
                   calls == 3,
               "production host assembles and registers all three phases"))
    {
        return false;
    }
    auto* scene = host.world()->create_scene("Character host");
    if (!scene)
    {
        return false;
    }
    auto floor = scene->create_node("Floor");
    auto collider = floor.add_component<Collider>();
    auto shape = read_settings(collider);
    shape.half_extents = {20, .5, 20};
    auto floor_t = floor.get_component<Transform>();
    auto pose = read_settings(floor_t);
    pose.position = {0, -.5, 0};
    auto player = scene->create_node("Player");
    auto motor = player.add_component<CharacterMotor>();
    auto controller = player.add_component<character::CharacterController>();
    auto view = scene->create_node("Camera");
    auto camera = view.add_component<Camera>();
    auto solver = view.add_component<camera::CameraController>();
    std::string error;
    if (!motor || !controller || !camera || !solver || !collider.set_settings(shape) ||
        !floor_t.set_settings(pose) ||
        !solver.set_settings(camera_definition(player.local_id()), error) ||
        !scene->set_active_camera(camera))
    {
        return false;
    }
    for (int i = 0; i < 5; ++i)
    {
        if (!tick(host))
        {
            return false;
        }
    }
    character::CharacterInput forward;
    forward.move = {0, 1};
    for (int i = 0; i < 20; ++i)
    {
        if (!controller.set_input(forward) || !tick(host))
        {
            return false;
        }
    }
    auto position = read_settings(player.get_component<Transform>()).position;
    auto camera_position = read_settings(view.get_component<Transform>()).position;
    if (!check(position.z() < -.5 && std::abs(camera_position.z() - position.z() - 4) < .001,
               "host camera observes same-frame physical result"))
    {
        return false;
    }
    if (!controller.set_enabled(false) || !tick(host))
    {
        return false;
    }
    auto stopped = read_settings(player.get_component<Transform>()).position;
    if (!check(std::hypot(stopped.x() - position.x(), stopped.z() - position.z()) < .001,
               "disable releases motion in the same frame"))
    {
        return false;
    }
    if (!controller.set_enabled(true) || !controller.set_input(forward) || !tick(host))
    {
        return false;
    }
    host.shutdown();
    return check(!host.world() && !host.window(), "host releases world and window");
}
} // namespace
int main(int, char**)
{
    return run() ? 0 : 1;
}
