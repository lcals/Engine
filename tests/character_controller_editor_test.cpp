#include "editor/asset_pipeline/components/scene_component_codec.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/scene_commands.hpp"
#include "plugins/editor/character_controller/character_controller_codec.hpp"
#include "plugins/editor/character_controller/character_controller_commands.hpp"
#include "plugins/runtime/character_controller/character_controller.hpp"
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
using engine::plugins::character_controller::CharacterController;

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
    settings.mode = engine::schema::CharacterView::first_person;
    settings.camera_node_id = 18446744073709551615ULL;
    settings.move_speed = 3;
    settings.pitch_min = -70;
    engine::schema::ComponentDataT source;
    source.payload.Set(settings);
    toml::table table;
    codec->write(source, table);
    engine::schema::ComponentDataT output;
    std::string error;
    if (!expect(codec->read(table, output, error) && output == source,
                "TOML preserves mode, settings and full-width persistent camera ID"))
    {
        return false;
    }
    table.insert_or_assign("mode", "unknown");
    if (!expect(!codec->read(table, output, error) && output == source,
                "invalid mode does not overwrite previous generated data"))
    {
        return false;
    }
    table.insert_or_assign("mode", "third_person");
    table.insert_or_assign("eye_height", -1.0);
    return expect(!codec->read(table, output, error) && output == source,
                  "invalid numeric settings are rejected transactionally");
}

bool commands_and_identity()
{
    engine::WorldSystem world;
    const engine::EngineConfig config;
    if (!world.initialize(config) || !world.component_registry().add<CharacterController>() ||
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
    edit["mode"] = "first_person";
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
    auto motor_edit = scene;
    motor_edit["nodeId"] = std::to_string(node->id());
    motor_edit["radius"] = 0.4;
    motor_edit["stepHeight"] = 0.25;
    if (!expect(commands.execute("set_character_motor", motor_edit, result, error),
                "core motor command remains part of generic scene commands"))
    {
        return false;
    }
    auto motor_query = scene;
    motor_query["nodeId"] = std::to_string(node->id());
    return expect(commands.execute("get_character_motor", motor_query, result, error) &&
                      result["stepHeight"] == 0.25 &&
                      commands.execute("undo", scene, result, error) &&
                      node->get_component<engine::CharacterMotor>() == nullptr,
                  "core motor edits use the same get and undo contract");
}

} // namespace

int main()
{
    return codec_roundtrip() && commands_and_identity() ? 0 : 1;
}
