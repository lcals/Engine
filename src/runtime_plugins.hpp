#pragma once

#include "runtime/app/runtime_host.hpp"
#include "runtime/world/world_system.hpp"

#if ENGINE_HAS_NAVIGATION_CROWD_PLUGIN
#include "plugins/runtime/navigation_crowd/navigation_crowd_system.hpp"
#endif

#if ENGINE_HAS_CHARACTER_CONTROLLER
#include "plugins/runtime/character_controller/character_camera_system.hpp"
#include "plugins/runtime/character_controller/character_controller_system.hpp"
#endif

#if ENGINE_HAS_DESTRUCTION_PLUGIN
#include "plugins/runtime/destruction/destruction_system.hpp"
#endif

#if ENGINE_HAS_TERRAIN_PLUGIN
#include "plugins/runtime/terrain/terrain_system.hpp"
#endif

#if ENGINE_HAS_EFFECTS_PLUGIN
#include "plugins/runtime/effects/effect_system.hpp"
#endif

namespace engine
{
class WindowSystem;
class PhysicsSystem;
} // namespace engine

namespace
{
bool configure_runtime_plugins(engine::RuntimeStage stage, engine::WorldSystem& world,
                               engine::WindowSystem& window, engine::PhysicsSystem& physics,
                               engine::NavigationSystem& navigation, engine::RenderSystem& renderer)
{
    (void)stage;
    (void)world;
    (void)window;
    (void)physics;
    (void)navigation;
    (void)renderer;
#if ENGINE_HAS_TERRAIN_PLUGIN
    if (stage == engine::RuntimeStage::before_physics)
    {
        std::unique_ptr<engine::System> terrain =
            std::make_unique<engine::plugins::terrain::TerrainSystem>(world, physics);
        if (!world.add_subsystem(terrain))
        {
            return false;
        }
    }
#endif
#if ENGINE_HAS_DESTRUCTION_PLUGIN
    if (stage == engine::RuntimeStage::before_physics)
    {
        std::unique_ptr<engine::System> destruction =
            std::make_unique<engine::plugins::destruction::DestructionSystem>(world, physics);
        if (!world.add_subsystem(destruction))
        {
            return false;
        }
    }
#endif
#if ENGINE_HAS_CHARACTER_CONTROLLER
    if (stage == engine::RuntimeStage::before_physics)
    {
        std::unique_ptr<engine::System> controller =
            std::make_unique<engine::plugins::character_controller::CharacterControllerSystem>(
                world);
        if (!world.add_subsystem(controller))
        {
            return false;
        }
    }
    else
    {
        std::unique_ptr<engine::System> camera =
            std::make_unique<engine::plugins::character_controller::CharacterCameraSystem>(world,
                                                                                           physics);
        if (!world.add_subsystem(camera))
        {
            return false;
        }
    }
#endif
#if ENGINE_HAS_NAVIGATION_CROWD_PLUGIN
    if (stage == engine::RuntimeStage::before_physics)
    {
        std::unique_ptr<engine::System> crowd =
            std::make_unique<engine::plugins::navigation_crowd::NavigationCrowdSystem>(world,
                                                                                       navigation);
        if (!world.add_subsystem(crowd))
        {
            return false;
        }
    }
#endif
#if ENGINE_HAS_EFFECTS_PLUGIN
    if (stage == engine::RuntimeStage::after_physics)
    {
        std::unique_ptr<engine::System> effects =
            std::make_unique<engine::plugins::effects::EffectSystem>(world, renderer);
        if (!world.add_subsystem(effects))
        {
            return false;
        }
    }
#endif
    return true;
}
} // namespace
