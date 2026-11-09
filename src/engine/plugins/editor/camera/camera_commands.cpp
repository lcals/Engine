#include "plugins/editor/camera/camera_commands.hpp"
#include "editor/commands/component_commands.hpp"
#include "plugins/runtime/camera/camera_controller.hpp"
#include "plugins/runtime/camera/camera_status_json.hpp"
#include "runtime/schema/control_json_generated.hpp"
#include "runtime/world/world_system.hpp"
namespace engine::plugins::camera_editor
{
bool register_camera_commands(WorldSystem& world, editor::SceneCommands& scenes,
                              editor::EditorCommands& commands)
{
    using Json = nlohmann::json;
    if (!editor::components::register_component<camera::CameraController,
                                                schema::CameraControllerDataT>(
            world, scenes, commands, "camera_controller",
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
                auto solver = node.get_component<camera::CameraController>();
                if (!solver)
                {
                    error = "camera controller not found";
                    return false;
                }
                return execute(solver, args, result, error);
            });
    };
    return transient("set_camera_parameters",
                     [](auto c, const Json& a, Json& r, std::string& e)
                     {
                         std::vector<schema::CameraParameterValueT> values;
                         if (!editor::components::fields(a, {"parameters"}, e) ||
                             !a.contains("parameters") ||
                             !camera::detail::read_parameters(c, a["parameters"], values) ||
                             !c.set_parameters(values))
                         {
                             e = "invalid parameter batch";
                             return false;
                         }
                         r = {{"updated", true}};
                         return true;
                     }) &&
           transient("trigger_camera_parameter",
                     [](auto c, const Json& a, Json& r, std::string& e)
                     {
                         if (!editor::components::fields(a, {"name", "reset"}, e) ||
                             !a.contains("name") || !a["name"].is_string() ||
                             (a.contains("reset") && !a["reset"].is_boolean()))
                         {
                             return false;
                         }
                         const bool ok =
                             a.value("reset", false)
                                 ? c.reset_trigger(a["name"].template get<std::string>())
                                 : c.trigger(a["name"].template get<std::string>());
                         r = {{"updated", ok}};
                         return ok;
                     }) &&
           transient("set_camera_target",
                     [&world](auto c, const Json& a, Json& r, std::string& e)
                     {
                         if (!editor::components::fields(
                                 a, {"slot", "targetNodeId", "restoreDefault"}, e) ||
                             !a.contains("slot") || !a["slot"].is_string() ||
                             (a.contains("restoreDefault") && !a["restoreDefault"].is_boolean()))
                         {
                             return false;
                         }
                         const auto slot = a["slot"].template get<std::string>();
                         bool ok = false;
                         if (a.value("restoreDefault", false))
                         {
                             if (a.contains("targetNodeId"))
                             {
                                 return false;
                             }
                             ok = c.clear_target_override(slot);
                         }
                         else
                         {
                             if (!a.contains("targetNodeId"))
                             {
                                 return false;
                             }
                             Node target;
                             if (!a["targetNodeId"].is_null())
                             {
                                 std::uint64_t id = 0;
                                 if (!schema::control_json::read(a["targetNodeId"], id, e, false,
                                                                 "targetNodeId"))
                                 {
                                     return false;
                                 }
                                 target = world.find_node(id);
                                 if (!target)
                                 {
                                     return false;
                                 }
                             }
                             ok = c.set_target(slot, target);
                         }
                         r = {{"updated", ok}};
                         return ok;
                     }) &&
           transient("get_camera_status",
                     [](auto c, const Json& a, Json& r, std::string& e)
                     {
                         camera::CameraStatus status;
                         if (!editor::components::fields(a, {}, e) || !c.get_status(status))
                         {
                             return false;
                         }
                         r = camera::detail::status_json(status);
                         return true;
                     });
}
} // namespace engine::plugins::camera_editor
