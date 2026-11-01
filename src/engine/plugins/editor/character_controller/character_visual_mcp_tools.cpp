#include "plugins/editor/character_controller/character_visual_mcp_tools.hpp"

#include "plugins/editor/mcp/component_mcp_tools.hpp"

namespace engine::plugins::character_controller
{

bool register_character_visual_mcp_tools(mcp::McpSystem& system, mcp::EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    return mcp::tools::component_tool(
               system, editor, "set_character_visual",
               "Classify a character visual subtree as body, head or first-person geometry; "
               "undoable.",
               {{"enabled", {{"type", "boolean"}}},
                {"kind", {{"type", "string"}, {"enum", {"body", "head", "first_person"}}}}}) &&
           mcp::tools::component_tool(system, editor, "get_character_visual",
                                      "Read character visual classification.", Json::object());
}

} // namespace engine::plugins::character_controller
