#include "plugins/editor/character_controller/character_camera_commands.hpp"
#include "editor/commands/component_commands.hpp"
#include "plugins/runtime/character_controller/character_camera.hpp"
#include "runtime/schema/control_json_generated.hpp"
namespace engine::plugins::character_controller
{
bool register_character_camera_commands(WorldSystem& world, editor::SceneCommands& scenes,
                                        editor::EditorCommands& commands)
{
    using Json = nlohmann::json;
    if (!editor::components::register_component<CharacterCamera, schema::CharacterCameraDataT>(
            world, scenes, commands, "character_camera",
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
    return true;
}
} // namespace engine::plugins::character_controller
