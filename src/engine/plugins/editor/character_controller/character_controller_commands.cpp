#include "plugins/editor/character_controller/character_controller_commands.hpp"

#include "editor/commands/components/component_edit.hpp"
#include "editor/commands/scene_commands.hpp"
#include "plugins/runtime/character_controller/character_controller.hpp"
#include "runtime/scene/camera.hpp"

namespace engine::plugins::character_controller
{
namespace
{

using Json = nlohmann::json;

bool read(WorldSystem& world, const Json& arguments, schema::CharacterControllerDataT& value,
          std::string& error)
{
    if (!editor::components::fields(arguments,
                                    {"enabled", "mode", "moveSpeed", "jumpSpeed", "lookSensitivity",
                                     "eyeHeight", "cameraDistance", "pitchMin", "pitchMax",
                                     "cameraNodeId"},
                                    error))
    {
        return false;
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
            error = "Invalid controller mode";
            return false;
        }
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
        !number("lookSensitivity", value.look_sensitivity) ||
        !number("eyeHeight", value.eye_height) ||
        !number("cameraDistance", value.camera_distance) || !number("pitchMin", value.pitch_min) ||
        !number("pitchMax", value.pitch_max))
    {
        return false;
    }
    if (arguments.contains("cameraNodeId"))
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
    return {{"mode", schema::EnumNameCharacterView(value.mode)},
            {"moveSpeed", value.move_speed},
            {"jumpSpeed", value.jump_speed},
            {"lookSensitivity", value.look_sensitivity},
            {"eyeHeight", value.eye_height},
            {"cameraDistance", value.camera_distance},
            {"pitchMin", value.pitch_min},
            {"pitchMax", value.pitch_max},
            {"cameraLocalId", std::to_string(value.camera_node_id)},
            {"cameraNodeId", std::move(camera_id)}};
}

} // namespace

bool register_controller_commands(WorldSystem& world, editor::SceneCommands& scene_commands,
                                  editor::EditorCommands& commands)
{
    return commands.register_command(
               "set_character_controller", true,
               [&world, &scene_commands](const Json& arguments, Json& result,
                                         std::unique_ptr<editor::EditorEdit>& edit,
                                         std::string& error)
               {
                   auto* node = editor::components::target(world, arguments, error);
                   if (node == nullptr)
                   {
                       return false;
                   }
                   const auto* component = node->get_component<CharacterController>();
                   std::optional<schema::ComponentDataT> before;
                   if (component != nullptr)
                   {
                       before.emplace();
                       if (!component->build_data(*before))
                       {
                           error = "Cannot snapshot character controller";
                           return false;
                       }
                   }
                   auto value = component != nullptr ? component->settings()
                                                     : schema::CharacterControllerDataT{};
                   if (!read(world, arguments, value, error))
                   {
                       return false;
                   }
                   schema::ComponentDataT after;
                   after.payload.Set(value);
                   after.enabled = component == nullptr || component->enabled();
                   if (arguments.contains("enabled"))
                   {
                       if (!arguments["enabled"].is_boolean())
                       {
                           error = "enabled must be boolean";
                           return false;
                       }
                       after.enabled = arguments["enabled"].get<bool>();
                   }
                   result = write(world, value);
                   result["enabled"] = after.enabled;
                   result["sceneId"] = std::to_string(node->scene().id());
                   result["nodeId"] = std::to_string(node->id());
                   auto identity = scene_commands.node_identity(node->id());
                   if (!identity)
                   {
                       error = "Cannot resolve stable node identity";
                       return false;
                   }
                   edit = std::make_unique<editor::components::ComponentEdit>(
                       world, node->scene().id(), std::move(identity), std::move(before),
                       std::move(after));
                   return true;
               }) &&
           commands.register_command(
               "get_character_controller", false,
               [&world](const Json& arguments, Json& result, std::unique_ptr<editor::EditorEdit>&,
                        std::string& error)
               {
                   if (!editor::components::fields(arguments, {}, error))
                   {
                       return false;
                   }
                   const auto* node = editor::components::target(world, arguments, error);
                   const auto* component =
                       node != nullptr ? node->get_component<CharacterController>() : nullptr;
                   if (component == nullptr)
                   {
                       if (error.empty())
                       {
                           error = "CharacterController not found";
                       }
                       return false;
                   }
                   result = write(world, component->settings());
                   result["enabled"] = component->enabled();
                   result["sceneId"] = std::to_string(node->scene().id());
                   result["nodeId"] = std::to_string(node->id());
                   return true;
               });
}

} // namespace engine::plugins::character_controller
