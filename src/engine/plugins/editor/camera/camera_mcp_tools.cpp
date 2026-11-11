#include "plugins/editor/camera/camera_mcp_tools.hpp"
#include "plugins/editor/mcp/editor_tool_registration.hpp"
#include "runtime/schema/control_json_generated.hpp"
namespace engine::plugins::camera_editor
{
bool register_camera_mcp_tools(mcp::McpSystem& system, mcp::EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    if (!mcp::tools::component_tool(
            system, editor, "set_camera_controller",
            "Replace the complete generated CameraControllerData settings; validated and undoable. "
            "Field names use the runtime schema's snake_case; enums use names; persistent node IDs "
            "are scene-local strings.",
            {{"settings", schema::control_json::describe(
                              static_cast<const schema::CameraControllerDataT*>(nullptr))},
             {"enabled", {{"type", "boolean"}}}},
            {"settings"}) ||
        !mcp::tools::component_tool(system, editor, "get_camera_controller",
                                    "Read the complete generated settings.", Json::object()))
    {
        return false;
    }
    return mcp::tools::component_tool(
               system, editor, "set_camera_parameters",
               "Set a transient batch of declared number/bool parameters; no undo or save.",
               {{"parameters", {{"type", "object"}}}}, {"parameters"}) &&
           mcp::tools::component_tool(
               system, editor, "trigger_camera_parameter", "Set or reset one declared trigger.",
               {{"name", {{"type", "string"}}}, {"reset", {{"type", "boolean"}}}}, {"name"}) &&
           mcp::tools::component_tool(
               system, editor, "set_camera_target",
               "Set a transient target. null explicitly unbinds; restoreDefault restores the "
               "scene-local default.",
               {{"slot", {{"type", "string"}}},
                {"targetNodeId", {{"type", Json::array({"string", "null"})}}},
                {"restoreDefault", {{"type", "boolean"}}}},
               {"slot"}) &&
           mcp::tools::component_tool(system, editor, "get_camera_status",
                                      "Read committed camera output and diagnostics.",
                                      Json::object());
}
} // namespace engine::plugins::camera_editor
