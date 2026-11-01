#include "plugins/editor/character_controller/character_controller_commands.hpp"

#include "plugins/editor/character_controller/character_camera_commands.hpp"
#include "plugins/editor/character_controller/character_visual_commands.hpp"
#include "plugins/editor/character_controller/component_commands.hpp"
#include "plugins/runtime/character_controller/character_controller.hpp"
#include "runtime/scene/camera.hpp"

namespace engine::plugins::character_controller
{
namespace
{

using Json = nlohmann::json;

bool read(WorldSystem& world, Node&, const Json& arguments, schema::CharacterControllerDataT& value,
          std::string& error)
{
    if (!editor::components::fields(
            arguments, {"enabled", "moveSpeed", "jumpSpeed", "lookSensitivity", "cameraNodeId"},
            error))
    {
        return false;
    }
    const auto number = [&](std::string_view name, float& output)
    {
        if (arguments.contains(name) && !editor::components::number(arguments[name], output))
        {
            error = "Invalid controller field: " + std::string(name);
            return false;
        }
        return true;
    };
    if (!number("moveSpeed", value.move_speed) || !number("jumpSpeed", value.jump_speed) ||
        !number("lookSensitivity", value.look_sensitivity))
    {
        return false;
    }
    if (arguments.contains("cameraNodeId") &&
        (arguments["cameraNodeId"].is_null() || arguments["cameraNodeId"] == "0"))
    {
        value.camera_node_id = 0;
    }
    else if (arguments.contains("cameraNodeId"))
    {
        Json target = {{"sceneId", arguments["sceneId"]}, {"nodeId", arguments["cameraNodeId"]}};
        auto* camera = editor::components::target(world, target, error);
        if (camera == nullptr || camera->get_component<Camera>() == nullptr)
        {
            if (error.empty())
            {
                error = "cameraNodeId must identify a Camera in this scene";
            }
            return false;
        }
        value.camera_node_id = camera->info().id;
    }
    if (!CharacterController::valid_settings(value))
    {
        error = "Invalid controller settings";
        return false;
    }
    return true;
}

Json write(const WorldSystem& world, const schema::CharacterControllerDataT& value)
{
    Json camera_id = nullptr;
    if (const auto* scene = world.scene())
    {
        for (const auto& node : scene->nodes())
        {
            if (node->info().id == value.camera_node_id)
            {
                camera_id = std::to_string(node->id());
                break;
            }
        }
    }
    return {{"moveSpeed", value.move_speed},
            {"jumpSpeed", value.jump_speed},
            {"lookSensitivity", value.look_sensitivity},
            {"cameraLocalId", std::to_string(value.camera_node_id)},
            {"cameraNodeId", std::move(camera_id)}};
}

} // namespace

bool register_controller_commands(WorldSystem& world, editor::SceneCommands& scene_commands,
                                  editor::EditorCommands& commands)
{
    return register_character_component<CharacterController, schema::CharacterControllerDataT>(
               world, scene_commands, commands, "character_controller", read, write) &&
           register_character_camera_commands(world, scene_commands, commands) &&
           register_character_visual_commands(world, scene_commands, commands);
}

} // namespace engine::plugins::character_controller
