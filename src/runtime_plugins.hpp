#pragma once

#include "runtime/app/runtime_host.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/world/world_system.hpp"
#if ENGINE_HAS_CAMERA
#include "plugins/runtime/camera/camera_system.hpp"
#include "plugins/runtime/camera/camera_type_registry.hpp"
#endif
namespace engine::plugins::camera
{
class CameraTypeRegistry;
}
namespace engine::plugins::character_controller
{
class CharacterAbilityRegistry;
}

#if ENGINE_HAS_NAVIGATION_CROWD_PLUGIN
#include "plugins/runtime/navigation_crowd/navigation_crowd_system.hpp"
#endif

#if ENGINE_HAS_CHARACTER_CONTROLLER
#include "plugins/runtime/character_controller/character_ability_registry.hpp"
#include "plugins/runtime/character_controller/character_camera_parameter_system.hpp"
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
bool assemble_runtime_plugins(
    engine::RuntimeStage stage, engine::WorldSystem& world, engine::WindowSystem& window,
    engine::PhysicsSystem& physics, engine::NavigationSystem& navigation,
    engine::RenderSystem& renderer, engine::TaskSystem& background_tasks,
    const engine::plugins::camera::CameraTypeRegistry* camera_types,
    const engine::plugins::character_controller::CharacterAbilityRegistry* abilities)
{
    (void)stage;
    (void)world;
    (void)window;
    (void)physics;
    (void)navigation;
    (void)renderer;
    (void)background_tasks;
    (void)camera_types;
    (void)abilities;
    if (stage == engine::RuntimeStage::initialized)
    {
#if ENGINE_HAS_CAMERA
        if (!camera_types ||
            !engine::plugins::camera::register_camera_components(world, *camera_types))
        {
            return false;
        }
#endif
#if ENGINE_HAS_CHARACTER_CONTROLLER
        if (!abilities || !engine::plugins::character_controller::register_character_components(
                              world, *abilities))
        {
            return false;
        }
#endif
        return true;
    }

#if ENGINE_HAS_TERRAIN_PLUGIN
    if (stage == engine::RuntimeStage::before_physics)
    {
        std::unique_ptr<engine::System> terrain =
            std::make_unique<engine::plugins::terrain::TerrainSystem>(world, physics, &background_tasks);
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
            std::make_unique<engine::plugins::destruction::DestructionSystem>(world, physics, &background_tasks);
        if (!world.add_subsystem(destruction))
        {
            return false;
        }
    }
#endif
#if ENGINE_HAS_CHARACTER_CONTROLLER
    if (stage == engine::RuntimeStage::before_physics)
    {
        auto controller =
            std::make_unique<engine::plugins::character_controller::CharacterControllerSystem>(
                world);
        if (!physics.set_character_motor_driver(controller.get()))
        {
            return false;
        }
        std::unique_ptr<engine::System> system = std::move(controller);
        if (!world.add_subsystem(system))
        {
            return false;
        }
    }
    else if (stage == engine::RuntimeStage::after_physics)
    {
        std::unique_ptr<engine::System> parameters =
            std::make_unique<engine::plugins::character_controller::CharacterCameraParameterSystem>(
                world);
        if (!world.add_subsystem(parameters))
        {
            return false;
        }
    }
#endif
#if ENGINE_HAS_CAMERA
    if (stage == engine::RuntimeStage::after_physics)
    {
        std::unique_ptr<engine::System> camera =
            std::make_unique<engine::plugins::camera::CameraSystem>(world, &physics);
        if (!world.add_subsystem(camera))
        {
            return false;
        }
    }
#endif
#if ENGINE_HAS_CHARACTER_CONTROLLER
    if (stage == engine::RuntimeStage::after_physics)
    {
        std::unique_ptr<engine::System> visibility =
            std::make_unique<engine::plugins::character_controller::CharacterCameraSystem>(world);
        if (!world.add_subsystem(visibility))
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
