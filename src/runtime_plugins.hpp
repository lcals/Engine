#pragma once

#include "runtime/app/runtime_host.hpp"
#include "runtime/world/world_system.hpp"

#if ENGINE_HAS_CHARACTER_CONTROLLER
#include "plugins/runtime/character_controller/character_camera_system.hpp"
#include "plugins/runtime/character_controller/character_controller_system.hpp"
#endif

namespace engine
{
class WindowSystem;
class PhysicsSystem;
} // namespace engine

namespace
{
bool configure_runtime_plugins(engine::RuntimeStage stage, engine::WorldSystem& world,
                               engine::WindowSystem& window, engine::PhysicsSystem& physics)
{
#if ENGINE_HAS_CHARACTER_CONTROLLER
    if (stage == engine::RuntimeStage::before_physics)
    {
        std::unique_ptr<engine::System> controller =
            std::make_unique<engine::plugins::character_controller::CharacterControllerSystem>(
                world, &window);
        return world.add_subsystem(controller);
    }
    std::unique_ptr<engine::System> camera =
        std::make_unique<engine::plugins::character_controller::CharacterCameraSystem>(world,
                                                                                     physics);
    return world.add_subsystem(camera);
#else
    (void)stage;
    (void)world;
    (void)window;
    (void)physics;
    return true;
#endif
}
} // namespace
