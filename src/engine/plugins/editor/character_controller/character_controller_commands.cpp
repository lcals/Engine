#include "plugins/editor/character_controller/character_controller_commands.hpp"
#include "editor/commands/component_commands.hpp"
#include "plugins/editor/character_controller/character_camera_commands.hpp"
#include "plugins/editor/character_controller/character_visual_commands.hpp"
#include "plugins/runtime/character_controller/character_controller.hpp"
#include "plugins/runtime/character_controller/character_input_json.hpp"
#include "plugins/runtime/character_controller/character_status_json.hpp"
#include "runtime/schema/control_json_generated.hpp"
namespace engine::plugins::character_controller
{
bool register_controller_commands(WorldSystem& world, editor::SceneCommands& scenes,
                                  editor::EditorCommands& commands)
{
    using Json = nlohmann::json;
    if (!editor::components::register_component<CharacterController,
                                                schema::CharacterControllerDataT>(
            world, scenes, commands, "character_controller",
            [](Node, const Json& args, auto& data, auto&, std::string& error)
            {
                if (!editor::components::fields(args, {"enabled", "settings"}, error) ||
                    !args.contains("settings"))
                {
                    error = "settings is required";
                    return false;
                }
                return schema::control_json::read(args["settings"], data, error);
            },
            [](auto, const auto& data)
            { return Json{{"settings", schema::control_json::write(data)}}; }))
    {
        return false;
    }
    const auto transient = [&](std::string name, auto execute)
    {
        return commands.register_command(
            std::move(name), false,
            [&world, execute](const Json& args, Json& result, std::unique_ptr<editor::EditorEdit>&,
                              std::string& error)
            {
                auto node = editor::components::target(world, args, error);
                auto c = node.get_component<CharacterController>();
                if (!c)
                {
                    error = "character controller not found";
                    return false;
                }
                return execute(c, args, result, error);
            });
    };
    return transient("set_character_input",
                     [](auto c, const Json& a, Json& r, std::string& e)
                     {
                         CharacterInput input;
                         if (!editor::components::fields(a, {"input"}, e) || !a.contains("input") ||
                             !detail::read_input(a["input"], input, e) || !c.set_input(input))
                         {
                             if (e.empty())
                             {
                                 e = "invalid character input";
                             }
                             return false;
                         }
                         r = {{"updated", true}};
                         return true;
                     }) &&
           transient("clear_character_input",
                     [](auto c, const Json& a, Json& r, std::string& e)
                     {
                         if (!editor::components::fields(a, {}, e) || !c.clear_input())
                         {
                             return false;
                         }
                         r = {{"updated", true}};
                         return true;
                     }) &&
           transient("get_character_status",
                     [](auto c, const Json& a, Json& r, std::string& e)
                     {
                         CharacterStatus status;
                         if (!editor::components::fields(a, {}, e) || !c.get_status(status))
                         {
                             return false;
                         }
                         r = detail::status_json(status, false);
                         return true;
                     }) &&
           register_character_camera_commands(world, scenes, commands) &&
           register_character_visual_commands(world, scenes, commands);
}
} // namespace engine::plugins::character_controller
