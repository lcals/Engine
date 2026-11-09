#include "control_fixture.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/scene_commands.hpp"
#include "plugins/editor/camera/camera_commands.hpp"
#include "plugins/editor/camera/camera_controller_codec.hpp"
#include "plugins/editor/character_controller/character_controller_codec.hpp"
#include "plugins/editor/character_controller/character_controller_commands.hpp"
#include "runtime/schema/control_json_generated.hpp"
using namespace test_support;
using namespace engine;
namespace
{
bool run()
{
    // The process-scoped codec registry is destroyed before these explicitly borrowed host tables.
    static camera::CameraTypeRegistry types;
    static character::CharacterAbilityRegistry abilities;
    if (!camera::register_builtin_camera_types(types) || !types.freeze() ||
        !character::register_builtin_character_abilities(abilities) || !abilities.freeze() ||
        !character::register_controller_codec(abilities) ||
        !plugins::camera_editor::register_camera_codec(types))
    {
        return false;
    }
    auto settings = camera_definition(18446744073709551615ULL);
    schema::ComponentDataT source;
    source.payload.Set(settings);
    const auto* codec = editor::asset_pipeline::find_scene_component_codec("camera_controller");
    toml::table table;
    codec->write(source, table);
    schema::ComponentDataT result;
    std::string error;
    if (!check(codec->read(table, result, error) && result == source,
               "camera config TOML round-trip preserves uint64 IDs"))
    {
        return false;
    }
    table.insert("camera_distance", 4);
    if (!check(!codec->read(table, result, error) && result == source,
               "removed camera field is rejected atomically"))
    {
        return false;
    }
    schema::CharacterControllerDataT control;
    control.movement = std::make_unique<schema::CharacterMovementDataT>();
    control.facing = std::make_unique<schema::CharacterFacingDataT>();
    enable_abilities(control);
    source.payload.Set(control);
    codec = editor::asset_pipeline::find_scene_component_codec("character_controller");
    table.clear();
    codec->write(source, table);
    if (!check(codec->read(table, result, error) && result == source,
               "character abilities TOML round-trip"))
    {
        return false;
    }
    table.insert("move_speed", 3);
    if (!check(!codec->read(table, result, error) && result == source,
               "removed movement field is rejected"))
    {
        return false;
    }
    auto json = schema::control_json::write(control, true);
    schema::CharacterControllerDataT parsed;
    if (!check(schema::control_json::read(json, parsed, error, true) && parsed == control,
               "script generated object uses camelCase and numeric enum values"))
    {
        return false;
    }
    WorldSystem world;
    EngineConfig config;
    if (!world.initialize(config) || !camera::register_camera_components(world, types) ||
        !character::register_character_components(world, abilities) || !world.create_scene("Edits"))
    {
        return false;
    }
    editor::SceneCommands scenes(world);
    editor::EditorCommands commands([&]() { return world.scene() ? world.scene()->id() : 0; });
    if (!scenes.register_commands(commands) ||
        !character::register_controller_commands(world, scenes, commands) ||
        !plugins::camera_editor::register_camera_commands(world, scenes, commands))
    {
        return false;
    }
    auto node = world.scene()->create_node("Player");
    nlohmann::json identity = {{"sceneId", std::to_string(world.scene()->id())},
                               {"nodeId", std::to_string(node.id())}};
    auto args = identity;
    args["settings"] = schema::control_json::write(control);
    nlohmann::json reply;
    if (!commands.execute("set_character_controller", args, reply, error))
    {
        return check(false, error.c_str());
    }
    auto c = node.get_component<character::CharacterController>();
    auto original = read_settings(c);
    args["settings"]["movement"]["max_speed"] = -1;
    if (!check(!commands.execute("set_character_controller", args, reply, error) &&
                   read_settings(c) == original,
               "invalid editor edit leaves live component unchanged"))
    {
        return false;
    }
    args["settings"]["movement"]["max_speed"] = 7;
    if (!commands.execute("set_character_controller", args, reply, error) ||
        !check(read_settings(c).movement->max_speed == 7, "editor replaces settings"))
    {
        return false;
    }
    if (!commands.execute("undo", {{"sceneId", std::to_string(world.scene()->id())}}, reply, error))
    {
        return check(false, error.c_str());
    }
    c = node.get_component<character::CharacterController>();
    if (!check(read_settings(c).movement->max_speed == 4, "undo restores complete ability config"))
    {
        return false;
    }
    args = identity;
    args["input"] = {{"move", {0, 1}},
                     {"lookDelta", {0, 0}},
                     {"lookRate", {0, 0}},
                     {"toggleView", false},
                     {"actions", nlohmann::json::object()}};
    if (!commands.execute("set_character_input", args, reply, error) ||
        !commands.execute("get_character_status", identity, reply, error) ||
        !check(reply["inputSequence"] == "0",
               "MCP status preserves uint64 identity as decimal strings") ||
        !commands.execute("clear_character_input", identity, reply, error))
    {
        return check(false, error.c_str());
    }
    auto camera_node = world.scene()->create_node("Camera");
    if (!camera_node.add_component<Camera>())
    {
        return false;
    }
    identity["nodeId"] = std::to_string(camera_node.id());
    args = identity;
    args["settings"] = schema::control_json::write(camera_definition(node.local_id()));
    if (!commands.execute("set_camera_controller", args, reply, error))
    {
        return check(false, error.c_str());
    }
    args = identity;
    args["parameters"] = {{"yaw", 30}};
    if (!commands.execute("set_camera_parameters", args, reply, error))
    {
        return check(false, error.c_str());
    }
    auto solver = camera_node.get_component<camera::CameraController>();
    schema::CameraParameterValueT value;
    if (!check(solver.get_parameter("yaw", value) && value.float_value == 30,
               "runtime parameter command"))
    {
        return false;
    }
    return check(read_settings(solver).parameters[0].default_value->float_value == 0,
                 "runtime parameter does not alter saved defaults");
}
} // namespace
int main()
{
    return run() ? 0 : 1;
}
