#pragma once

#include "runtime/world/world_system.hpp"

#if ENGINE_HAS_CHARACTER_CONTROLLER
#include "plugins/runtime/character_controller/character_controller_system.hpp"
#endif

namespace engine
{
class WindowSystem;
class PhysicsSystem;
} // namespace engine

namespace
{
bool configure_runtime_plugins(engine::WorldSystem& world, engine::WindowSystem& window,
                               engine::PhysicsSystem& physics)
{
#if ENGINE_HAS_CHARACTER_CONTROLLER
    std::unique_ptr<engine::System> controller =
        std::make_unique<engine::plugins::character_controller::CharacterControllerSystem>(
            world, physics, &window);
    return world.add_subsystem(controller);
#else
    (void)world;
    (void)window;
    (void)physics;
    return true;
#endif
}
} // namespace
