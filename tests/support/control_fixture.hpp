#pragma once
#include "plugins/runtime/camera/camera_controller.hpp"
#include "plugins/runtime/camera/camera_system.hpp"
#include "plugins/runtime/camera/camera_type_registry.hpp"
#include "plugins/runtime/character_controller/character_ability_registry.hpp"
#include "plugins/runtime/character_controller/character_camera.hpp"
#include "plugins/runtime/character_controller/character_camera_parameter_system.hpp"
#include "plugins/runtime/character_controller/character_camera_system.hpp"
#include "plugins/runtime/character_controller/character_controller.hpp"
#include "plugins/runtime/character_controller/character_controller_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "scene_snapshot.hpp"
#include <cstdio>
namespace test_support
{
namespace camera = engine::plugins::camera;
namespace character = engine::plugins::character_controller;
inline bool check(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "control test: %s\n", message);
    }
    return value;
}
inline std::unique_ptr<engine::schema::CameraFloatInputDataT>
camera_float(float value, std::string parameter = {})
{
    auto v = std::make_unique<engine::schema::CameraFloatInputDataT>();
    v->constant = value;
    v->parameter = std::move(parameter);
    return v;
}
inline engine::schema::CameraControllerDataT camera_definition(std::uint64_t subject = 0)
{
    using namespace engine::schema;
    CameraControllerDataT d;
    d.module_data = std::make_unique<CameraModuleSetDataT>();
    d.machine = std::make_unique<CameraStateMachineDataT>();
    d.machine->initial_state = "idle";
    CameraStateDataT state;
    state.id = "idle";
    state.rig = "base";
    d.machine->states.push_back(state);
    CameraLensDataT lens;
    lens.id = 1;
    lens.fov = camera_float(60);
    lens.near_clip = camera_float(.1F);
    lens.far_clip = camera_float(100);
    lens.orthographic_size = camera_float(5);
    d.module_data->lens.push_back(std::move(lens));
    CameraRigDataT rig;
    rig.id = "base";
    CameraModuleRefT ref;
    ref.id = 1;
    ref.type = "lens";
    rig.modules.push_back(ref);
    ref.id = 2;
    if (subject)
    {
        CameraTargetDataT target;
        target.name = "subject";
        target.node_local_id = subject;
        d.targets.push_back(target);
        for (const auto* name : {"yaw", "pitch"})
        {
            CameraParameterDataT p;
            p.name = name;
            p.type = CameraParameterType::float_value;
            p.default_value = std::make_unique<CameraParameterValueT>();
            p.default_value->name = name;
            p.has_range = true;
            p.minimum = name == std::string("yaw") ? -180 : -80;
            p.maximum = -p.minimum;
            d.parameters.push_back(std::move(p));
        }
        CameraOrbitDataT orbit;
        orbit.id = 2;
        orbit.follow_slot = "subject";
        orbit.pivot_offset = {0, 1.5, 0};
        orbit.distance = camera_float(4);
        orbit.yaw = camera_float(0, "yaw");
        orbit.pitch = camera_float(0, "pitch");
        orbit.shoulder_offset = camera_float(0);
        d.module_data->orbit.push_back(std::move(orbit));
        ref.type = "orbit";
    }
    else
    {
        CameraFixedDataT fixed;
        fixed.id = 2;
        fixed.position = {0, 2, 4};
        d.module_data->fixed.push_back(fixed);
        ref.type = "fixed";
    }
    rig.modules.push_back(ref);
    d.rigs.push_back(std::move(rig));
    return d;
}
inline void enable_abilities(engine::schema::CharacterControllerDataT& d)
{
    using namespace engine::schema;
    d.actions.clear();
    for (const auto* name : {"jump", "sprint", "crouch"})
    {
        CharacterActionDataT a;
        a.name = name;
        a.buffer_seconds = name == std::string("jump") ? .15F : 0;
        d.actions.push_back(a);
    }
    d.abilities.clear();
    for (const auto* name : {"crouch", "jump", "sprint"})
    {
        CharacterAbilityRefT a;
        a.id = name;
        a.type = name;
        a.enabled = true;
        a.priority = 30 - static_cast<int>(d.abilities.size()) * 10;
        d.abilities.push_back(a);
    }
    d.ability_data = std::make_unique<CharacterAbilitySetDataT>();
    CharacterCrouchDataT crouch;
    crouch.id = "crouch";
    crouch.action = "crouch";
    crouch.half_height = .3F;
    crouch.speed = 2;
    d.ability_data->crouch.push_back(crouch);
    CharacterJumpDataT jump;
    jump.id = "jump";
    jump.action = "jump";
    jump.coyote_seconds = .1F;
    jump.hold_seconds = .2F;
    jump.held_gravity_scale = .7F;
    jump.released_gravity_scale = 1.5F;
    d.ability_data->jump.push_back(jump);
    CharacterSprintDataT sprint;
    sprint.id = "sprint";
    sprint.action = "sprint";
    d.ability_data->sprint.push_back(sprint);
}
struct ControlFixture
{
    camera::CameraTypeRegistry camera_types;
    character::CharacterAbilityRegistry abilities;
    engine::WorldSystem world;
    character::CharacterControllerSystem controllers{world};
    engine::PhysicsSystem physics{&world};
    character::CharacterCameraParameterSystem parameters{world};
    camera::CameraSystem cameras{world, &physics};
    character::CharacterCameraSystem visibility{world};
    engine::Node player, view;
    engine::Node box(const char* name, engine::Vec3 position, engine::Vec3 extents)
    {
        auto n = world.scene()->create_node(name);
        auto t = n.get_component<engine::Transform>();
        auto d = read_settings(t);
        d.position = position;
        auto c = n.add_component<engine::Collider>();
        auto shape = read_settings(c);
        shape.half_extents = extents;
        return t.set_settings(d) && c.set_settings(shape) ? n : engine::Node{};
    }
    bool initialize(bool with_abilities = true)
    {
        engine::EngineConfig config;
        if (!camera::register_builtin_camera_types(camera_types) || !camera_types.freeze() ||
            !character::register_builtin_character_abilities(abilities) || !abilities.freeze() ||
            !world.initialize(config) || !camera::register_camera_components(world, camera_types) ||
            !character::register_character_components(world, abilities) ||
            !controllers.initialize(config) || !physics.set_character_motor_driver(&controllers) ||
            !physics.initialize(config) || !parameters.initialize(config) ||
            !cameras.initialize(config) || !visibility.initialize(config) ||
            !world.create_scene("Controls"))
        {
            return false;
        }
        if (!box("Floor", {0, -.5, 0}, {30, .5, 30}))
        {
            return false;
        }
        player = world.scene()->create_node("Player");
        view = world.scene()->create_node("Camera");
        if (!player.add_component<engine::CharacterMotor>() ||
            !player.add_component<character::CharacterController>() ||
            !view.add_component<engine::Camera>() ||
            !view.add_component<camera::CameraController>())
        {
            return false;
        }
        auto controller = player.get_component<character::CharacterController>();
        auto data = read_settings(controller);
        if (with_abilities)
        {
            enable_abilities(data);
        }
        std::string error;
        if (!controller.set_settings(data, error) ||
            !view.get_component<camera::CameraController>().set_settings(
                camera_definition(player.local_id()), error))
        {
            return check(false, error.c_str());
        }
        return step({}, 4);
    }
    bool frame(const character::CharacterInput& input, double dt = 1.0 / 60.0)
    {
        return player.get_component<character::CharacterController>().set_input(input) &&
               controllers.tick(dt) && physics.tick(dt) && parameters.tick(dt) &&
               cameras.tick(dt) && visibility.tick(dt);
    }
    bool step(const character::CharacterInput& input, unsigned count)
    {
        for (unsigned i = 0; i < count; ++i)
        {
            if (!frame(input))
            {
                return false;
            }
        }
        return true;
    }
    character::CharacterStatus status() const
    {
        character::CharacterStatus out;
        if (!player.get_component<character::CharacterController>().get_status(out))
        {
            std::fputs("missing status\n", stderr);
        }
        return out;
    }
};
} // namespace test_support
