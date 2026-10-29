#include "plugins/editor/character_controller/character_controller_mcp_tools.hpp"

#include "plugins/editor/mcp/component_mcp_tools.hpp"

namespace engine::plugins::character_controller
{

bool register_controller_mcp_tools(mcp::McpSystem& system, mcp::EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    const Json number = {{"type", "number"}};
    return mcp::tools::component_tool(
               system, editor, "set_character_controller",
               "Add or edit first/third person controller settings; undoable. cameraNodeId "
               "identifies a Camera in this scene.",
               {{"enabled", {{"type", "boolean"}}},
                {"mode", {{"type", "string"}, {"enum", {"first_person", "third_person"}}}},
                {"moveSpeed", number},
                {"jumpSpeed", number},
                {"lookSensitivity", number},
                {"eyeHeight", number},
                {"cameraDistance", number},
                {"pitchMin", number},
                {"pitchMax", number},
                {"cameraNodeId", {{"type", "string"}, {"minLength", 1}}}}) &&
           mcp::tools::component_tool(system, editor, "get_character_controller",
                                      "Read controller settings and resolve the camera's runtime "
                                      "and persistent node identities.",
                                      Json::object());
}

} // namespace engine::plugins::character_controller
