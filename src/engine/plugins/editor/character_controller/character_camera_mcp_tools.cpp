#include "plugins/editor/character_controller/character_camera_mcp_tools.hpp"

#include "plugins/editor/mcp/component_mcp_tools.hpp"

namespace engine::plugins::character_controller
{

bool register_character_camera_mcp_tools(mcp::McpSystem& system, mcp::EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    const Json number = {{"type", "number"}};
    return mcp::tools::component_tool(
               system, editor, "set_character_camera",
               "Add or edit follow-camera settings on a Camera node; undoable. targetNodeId "
               "identifies a node in this scene, or the string 0 to detach.",
               {{"enabled", {{"type", "boolean"}}},
                {"targetNodeId", {{"type", "string"}, {"minLength", 1}}},
                {"mode", {{"type", "string"}, {"enum", {"first_person", "third_person"}}}},
                {"eyeHeight", number},
                {"cameraDistance", number},
                {"pitchMin", number},
                {"pitchMax", number},
                {"collisionRadius", number},
                {"collisionMargin", number}}) &&
           mcp::tools::component_tool(system, editor, "get_character_camera",
                                      "Read follow-camera settings and target node identities.",
                                      Json::object());
}

} // namespace engine::plugins::character_controller
