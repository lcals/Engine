#include "plugins/editor/character_controller/character_controller_mcp_tools.hpp"

#include "plugins/editor/character_controller/character_camera_mcp_tools.hpp"
#include "plugins/editor/character_controller/character_visual_mcp_tools.hpp"
#include "plugins/editor/mcp/component_mcp_tools.hpp"

namespace engine::plugins::character_controller
{

bool register_controller_mcp_tools(mcp::McpSystem& system, mcp::EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    const Json number = {{"type", "number"}};
    return mcp::tools::component_tool(
               system, editor, "set_character_controller",
               "Add or edit player input settings; undoable. cameraNodeId identifies a Camera "
               "in this scene, or the string 0 to detach. "
               "Follow settings belong to character_camera.",
               {{"enabled", {{"type", "boolean"}}},
                {"moveSpeed", number},
                {"jumpSpeed", number},
                {"lookSensitivity", number},
                {"cameraNodeId", {{"type", "string"}, {"minLength", 1}}}}) &&
           mcp::tools::component_tool(system, editor, "get_character_controller",
                                      "Read controller settings and resolve the camera's runtime "
                                      "and persistent node identities.",
                                      Json::object()) &&
           register_character_camera_mcp_tools(system, editor) &&
           register_character_visual_mcp_tools(system, editor);
}

} // namespace engine::plugins::character_controller
