#include "editor/asset_pipeline/components/scene_component_codec.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/scene_commands.hpp"
#include "plugins/editor/character_controller/character_controller_codec.hpp"
#include "plugins/editor/character_controller/character_controller_commands.hpp"
#include "plugins/runtime/character_controller/character_camera.hpp"
#include "plugins/runtime/character_controller/character_controller.hpp"
#include "plugins/runtime/character_controller/character_visual.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <string>

namespace
{

using Json = nlohmann::json;
using engine::plugins::character_controller::CharacterCamera;
using engine::plugins::character_controller::CharacterController;
using engine::plugins::character_controller::CharacterVisual;

bool expect(bool value, const char* description)
{
    if (!value)
    {
        std::fprintf(stderr, "character editor test: %s\n", description);
    }
    return value;
}

bool codec_roundtrip()
{
    namespace plugin = engine::plugins::character_controller;
    namespace pipeline = engine::editor::asset_pipeline;
    if (!expect(plugin::register_controller_codec() && plugin::register_controller_codec(),
                "explicit codec registration is repeatable"))
    {
        return false;
    }
    const auto* codec = pipeline::find_scene_component_codec("character_controller");
    if (!expect(codec && codec == pipeline::find_scene_component_codec(
                                      CharacterController::component_type),
                "codec can be found by public name and generated type"))
    {
        return false;
    }
    engine::schema::CharacterControllerDataT settings;
    settings.camera_node_id = 18446744073709551615ULL;
    settings.move_speed = 3;
    engine::schema::ComponentDataT source;
    source.payload.Set(settings);
    toml::table table;
    codec->write(source, table);
    engine::schema::ComponentDataT output;
    std::string error;
    if (!expect(codec->read(table, output, error) && output == source,
                "TOML preserves input settings and full-width persistent camera ID"))
    {
        return false;
    }
    for (const auto name : {"mode", "eye_height", "camera_distance", "pitch_min", "pitch_max"})
    {
        auto legacy = table;
        legacy.insert(name, 1.0);
        error.clear();
        if (!expect(!codec->read(legacy, output, error) && output == source &&
                        error.find("character_camera") != std::string::npos,
                    "legacy controller camera fields fail with an explicit migration message"))
        {
            return false;
        }
    }
    table.insert_or_assign("move_speed", -1.0);
    if (!expect(!codec->read(table, output, error) && output == source,
                "invalid controller settings are rejected transactionally"))
    {
        return false;
    }
    const auto* camera_codec = pipeline::find_scene_component_codec("character_camera");
    const auto* visual_codec = pipeline::find_scene_component_codec("character_visual");
    const auto* motor_codec = pipeline::find_scene_component_codec("character_motor");
    if (!expect(camera_codec && visual_codec && motor_codec,
                "plugin registration installs camera and visual codecs beside the core motor"))
    {
        return false;
    }
    engine::schema::CharacterCameraDataT camera;
    camera.target_node_id = 18446744073709551615ULL;
    camera.mode = engine::schema::CharacterView::first_person;
    camera.pitch_min = -70;
    camera.collision_radius = 0.25F;
    source.payload.Set(camera);
    table.clear();
    camera_codec->write(source, table);
    if (!expect(camera_codec->read(table, output, error) && output == source,
                "camera TOML preserves target identity, view settings and collision parameters"))
    {
        return false;
    }
    table.insert_or_assign("collision_radius", -1.0);
    if (!expect(!camera_codec->read(table, output, error) && output == source,
                "invalid camera settings leave the prior payload unchanged"))
    {
        return false;
    }
    engine::schema::CharacterVisualDataT visual;
    visual.kind = engine::schema::CharacterVisualKind::first_person;
    source.payload.Set(visual);
    table.clear();
    visual_codec->write(source, table);
    if (!expect(visual_codec->read(table, output, error) && output == source,
                "visual TOML preserves first-person classification"))
    {
        return false;
    }
    table.insert_or_assign("kind", "invalid");
    if (!expect(!visual_codec->read(table, output, error) && output == source,
                "invalid visual classification is rejected transactionally"))
    {
        return false;
    }
    engine::schema::CharacterMotorDataT motor;
    motor.external_damping = 12.0F;
    source.payload.Set(motor);
    table.clear();
    motor_codec->write(source, table);
    if (!expect(motor_codec->read(table, output, error) && output == source,
                "motor TOML preserves external velocity damping"))
    {
        return false;
    }
    table.insert_or_assign("external_damping", 1001.0);
    return expect(!motor_codec->read(table, output, error) && output == source,
                  "out-of-range motor damping leaves prior data unchanged");
}

bool commands_and_identity()
{
    engine::WorldSystem world;
    const engine::EngineConfig config;
    if (!world.initialize(config) || !world.component_registry().add<CharacterController>() ||
        !world.component_registry().add<CharacterCamera>() ||
        !world.component_registry().add<CharacterVisual>() ||
        world.create_scene("Controller edits") == nullptr)
    {
        return false;
    }
    engine::editor::SceneCommands scene_commands(world);
    engine::editor::EditorCommands commands([&world]()
                                            { return world.scene() ? world.scene()->id() : 0; });
    if (!scene_commands.register_commands(commands) ||
        !engine::plugins::character_controller::register_controller_commands(world, scene_commands,
                                                                             commands))
    {
        return false;
    }
    auto* camera = world.scene()->create_node("Camera");
    if (camera == nullptr || camera->add_component<engine::Camera>() == nullptr)
    {
        return false;
    }
    Json result;
    std::string error;
    const Json scene = {{"sceneId", std::to_string(world.scene()->id())}};
    auto create = scene;
    create["name"] = "Player";
    create["component"] = "empty";
    if (!commands.execute("create_node", create, result, error))
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return false;
    }
    auto* node = world.scene()->nodes().back().get();
    if (!node || node->name() != "Player")
    {
        return false;
    }
    const auto id = std::to_string(node->id());
    auto identity = scene;
    identity["nodeId"] = id;
    auto edit = identity;
    edit["cameraNodeId"] = std::to_string(camera->id());
    edit["moveSpeed"] = 7.0;
    if (!expect(commands.execute("set_character_controller", edit, result, error),
                "typed editor command adds plugin component through World registry"))
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return false;
    }
    auto* controller = node->get_component<CharacterController>();
    if (!expect(controller && controller->settings().camera_node_id == camera->info().id &&
                    controller->settings().move_speed == 7 &&
                    commands.execute("get_character_controller", identity, result, error) &&
                    result["cameraNodeId"] == std::to_string(camera->id()),
                "commands translate runtime camera identity into persistent local identity"))
    {
        return false;
    }
    const auto history = commands.undo_count();
    edit["moveSpeed"] = -1;
    if (!expect(!commands.execute("set_character_controller", edit, result, error) &&
                    controller->settings().move_speed == 7 && commands.undo_count() == history,
                "invalid edits leave component and history unchanged") ||
        !expect(commands.execute("undo", scene, result, error) &&
                    !node->get_component<CharacterController>(),
                "undo removes newly added plugin component") ||
        !expect(commands.execute("undo", scene, result, error),
                "undo removes created player node") ||
        !expect(commands.execute("redo", scene, result, error),
                "redo recreates player under new runtime identity") ||
        !expect(commands.execute("redo", scene, result, error),
                "redo component resolves shared node alias"))
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return false;
    }
    node = nullptr;
    for (const auto& candidate : world.scene()->nodes())
    {
        if (candidate->name() == "Player")
        {
            node = candidate.get();
        }
    }
    if (!expect(node && node->get_component<CharacterController>() &&
                    node->get_component<CharacterController>()->settings().camera_node_id ==
                        camera->info().id,
                "component redo keeps persistent camera reference after node recreation"))
    {
        return false;
    }
    auto camera_identity = scene;
    camera_identity["nodeId"] = std::to_string(camera->id());
    auto camera_edit = camera_identity;
    camera_edit["targetNodeId"] = std::to_string(node->id());
    camera_edit["mode"] = "first_person";
    camera_edit["collisionRadius"] = 0.25;
    if (!expect(commands.execute("set_character_camera", camera_edit, result, error) &&
                    camera->get_component<CharacterCamera>() &&
                    camera->get_component<CharacterCamera>()->settings().target_node_id ==
                        node->info().id &&
                    commands.execute("get_character_camera", camera_identity, result, error) &&
                    result["targetNodeId"] == std::to_string(node->id()) &&
                    result["targetLocalId"] == std::to_string(node->info().id),
                "follow-camera commands translate target identity independently of player input"))
    {
        return false;
    }
    const auto camera_history = commands.undo_count();
    camera_edit["collisionRadius"] = -1;
    if (!expect(!commands.execute("set_character_camera", camera_edit, result, error) &&
                    camera->get_component<CharacterCamera>()->settings().collision_radius ==
                        0.25F &&
                    commands.undo_count() == camera_history &&
                    commands.execute("undo", scene, result, error) &&
                    camera->get_component<CharacterCamera>() == nullptr &&
                    commands.execute("redo", scene, result, error) &&
                    camera->get_component<CharacterCamera>()->settings().target_node_id ==
                        node->info().id,
                "invalid follow edits are atomic and valid edits support undo and redo"))
    {
        return false;
    }
    camera_edit = camera_identity;
    camera_edit["targetNodeId"] = "0";
    if (!expect(commands.execute("set_character_camera", camera_edit, result, error) &&
                    result["targetNodeId"].is_null() && result["targetLocalId"] == "0" &&
                    commands.execute("undo", scene, result, error),
                "zero follow target detaches and undo restores the persistent reference"))
    {
        return false;
    }
    auto player_identity = scene;
    player_identity["nodeId"] = std::to_string(node->id());
    camera_edit = player_identity;
    camera_edit["targetNodeId"] = std::to_string(camera->id());
    if (!expect(!commands.execute("set_character_camera", camera_edit, result, error) &&
                    node->get_component<CharacterCamera>() == nullptr,
                "follow-camera edit requires a Camera on its owning node"))
    {
        return false;
    }
    auto visual_edit = player_identity;
    visual_edit["kind"] = "head";
    if (!expect(commands.execute("set_character_visual", visual_edit, result, error) &&
                    commands.execute("get_character_visual", player_identity, result, error) &&
                    result["kind"] == "head" &&
                    node->get_component<CharacterVisual>()->settings().kind ==
                        engine::schema::CharacterVisualKind::head,
                "visual classification has independent typed set and get commands"))
    {
        return false;
    }
    const auto visual_history = commands.undo_count();
    visual_edit["kind"] = "invalid";
    if (!expect(!commands.execute("set_character_visual", visual_edit, result, error) &&
                    commands.undo_count() == visual_history &&
                    commands.execute("undo", scene, result, error) &&
                    node->get_component<CharacterVisual>() == nullptr,
                "invalid visual edits preserve history and undo removes the added marker"))
    {
        return false;
    }
    auto detached_controller = player_identity;
    detached_controller["cameraNodeId"] = "0";
    if (!expect(commands.execute("set_character_controller", detached_controller, result, error) &&
                    result["cameraNodeId"].is_null() && result["cameraLocalId"] == "0" &&
                    commands.execute("undo", scene, result, error),
                "controller camera reference is optional and undoable"))
    {
        return false;
    }
    auto legacy_edit = player_identity;
    legacy_edit["mode"] = "third_person";
    if (!expect(!commands.execute("set_character_controller", legacy_edit, result, error),
                "removed controller camera fields are rejected by editor commands"))
    {
        return false;
    }
    auto motor_edit = scene;
    motor_edit["nodeId"] = std::to_string(node->id());
    motor_edit["radius"] = 0.4;
    motor_edit["stepHeight"] = 0.25;
    motor_edit["externalDamping"] = 12.0;
    if (!expect(commands.execute("set_character_motor", motor_edit, result, error),
                "core motor command remains part of generic scene commands"))
    {
        return false;
    }
    auto motor_query = scene;
    motor_query["nodeId"] = std::to_string(node->id());
    const auto motor_history = commands.undo_count();
    motor_edit["externalDamping"] = 1001.0;
    if (!expect(!commands.execute("set_character_motor", motor_edit, result, error) &&
                    node->get_component<engine::CharacterMotor>()->settings().external_damping ==
                        12.0F &&
                    commands.undo_count() == motor_history,
                "external damping range is enforced without changing component or undo history"))
    {
        return false;
    }
    return expect(commands.execute("get_character_motor", motor_query, result, error) &&
                      result["stepHeight"] == 0.25 && result["externalDamping"] == 12.0 &&
                      commands.execute("undo", scene, result, error) &&
                      node->get_component<engine::CharacterMotor>() == nullptr,
                  "core motor edits use the same get and undo contract");
}

} // namespace

int main()
{
    return codec_roundtrip() && commands_and_identity() ? 0 : 1;
}
