#include "plugins/editor/effects/effect_mcp_tools.hpp"

#include "plugins/editor/mcp/editor_tool_registration.hpp"

namespace engine::plugins::effects
{

bool register_effect_mcp_tools(mcp::McpSystem& system, mcp::EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    return mcp::tools::component_tool(
               system, editor, "set_effect",
               "Create or edit an Effekseer effect component using an imported resource path; "
               "undoable. Empty effect clears the resource.",
               {{"enabled", {{"type", "boolean"}}},
                {"effect", {{"type", "string"}}},
                {"speed", {{"type", "number"}, {"minimum", 0}, {"maximum", 16}}},
                {"loop", {{"type", "boolean"}}},
                {"playOnAwake", {{"type", "boolean"}}}}) &&
           mcp::tools::component_tool(system, editor, "get_effect",
                                      "Read effect component settings and playback state.",
                                      Json::object()) &&
           mcp::tools::component_tool(system, editor, "play_effect",
                                      "Play or resume the current effect; does not edit the scene.",
                                      Json::object()) &&
           mcp::tools::component_tool(system, editor, "pause_effect",
                                      "Pause the current effect while retaining its particles.",
                                      Json::object()) &&
           mcp::tools::component_tool(system, editor, "stop_effect",
                                      "Stop the current effect and remove its active particles.",
                                      Json::object());
}

} // namespace engine::plugins::effects
