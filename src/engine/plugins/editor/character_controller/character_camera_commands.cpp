#include "plugins/editor/character_controller/character_camera_commands.hpp"

#include "plugins/editor/character_controller/component_commands.hpp"
#include "plugins/runtime/character_controller/character_camera.hpp"
#include "runtime/scene/camera.hpp"

namespace engine::plugins::character_controller
{
namespace
{

using Json = nlohmann::json;

bool read(WorldSystem& world, Node& node, const Json& arguments,
          schema::CharacterCameraDataT& value, std::string& error)
{
    if (!editor::components::fields(arguments,
                                    {"enabled", "targetNodeId", "mode", "eyeHeight",
                                     "cameraDistance", "pitchMin", "pitchMax", "collisionRadius",
                                     "collisionMargin"},
                                    error))
    {
        return false;
    }
    if (node.get_component<Camera>() == nullptr)
    {
        error = "CharacterCamera requires a Camera on the same node";
        return false;
    }
    if (arguments.contains("targetNodeId"))
    {
        if (arguments["targetNodeId"].is_null() || arguments["targetNodeId"] == "0")
        {
            value.target_node_id = 0;
        }
        else
        {
            Json target = {{"sceneId", arguments["sceneId"]},
                           {"nodeId", arguments["targetNodeId"]}};
            const auto* target_node = editor::components::target(world, target, error);
            if (target_node == nullptr)
            {
                return false;
            }
            value.target_node_id = target_node->info().id;
        }
    }
    if (arguments.contains("mode"))
    {
        if (arguments["mode"] == "first_person")
        {
            value.mode = schema::CharacterView::first_person;
        }
        else if (arguments["mode"] == "third_person")
        {
            value.mode = schema::CharacterView::third_person;
        }
        else
        {
            error = "Invalid character camera mode";
            return false;
        }
    }
    const auto number = [&](std::string_view name, float& output)
    {
        if (arguments.contains(name) && !editor::components::number(arguments[name], output))
        {
            error = "Invalid character camera field: " + std::string(name);
            return false;
        }
        return true;
    };
    return number("eyeHeight", value.eye_height) &&
           number("cameraDistance", value.camera_distance) && number("pitchMin", value.pitch_min) &&
           number("pitchMax", value.pitch_max) &&
           number("collisionRadius", value.collision_radius) &&
           number("collisionMargin", value.collision_margin);
}

Json write(const WorldSystem& world, const schema::CharacterCameraDataT& value)
{
    Json target_id = nullptr;
    if (const auto* scene = world.scene())
    {
        for (const auto& node : scene->nodes())
        {
            if (node->info().id == value.target_node_id)
            {
                target_id = std::to_string(node->id());
                break;
            }
        }
    }
    return {{"targetNodeId", std::move(target_id)},
            {"targetLocalId", std::to_string(value.target_node_id)},
            {"mode", schema::EnumNameCharacterView(value.mode)},
            {"eyeHeight", value.eye_height},
            {"cameraDistance", value.camera_distance},
            {"pitchMin", value.pitch_min},
            {"pitchMax", value.pitch_max},
            {"collisionRadius", value.collision_radius},
            {"collisionMargin", value.collision_margin}};
}

} // namespace

bool register_character_camera_commands(WorldSystem& world, editor::SceneCommands& scene_commands,
                                        editor::EditorCommands& commands)
{
    return register_character_component<CharacterCamera, schema::CharacterCameraDataT>(
        world, scene_commands, commands, "character_camera", read, write);
}

} // namespace engine::plugins::character_controller
