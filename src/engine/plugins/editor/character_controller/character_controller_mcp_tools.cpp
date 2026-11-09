#include "plugins/editor/character_controller/character_controller_mcp_tools.hpp"
#include "plugins/editor/character_controller/character_camera_mcp_tools.hpp"
#include "plugins/editor/character_controller/character_visual_mcp_tools.hpp"
#include "plugins/editor/mcp/editor_tool_registration.hpp"
#include "plugins/runtime/character_controller/character_input_json.hpp"
#include "runtime/schema/control_json_generated.hpp"
namespace engine::plugins::character_controller
{
bool register_controller_mcp_tools(mcp::McpSystem& system, mcp::EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    if (!mcp::tools::component_tool(
            system, editor, "set_character_controller",
            "Replace the complete generated CharacterControllerData settings; validated and "
            "undoable. Field names use the runtime schema's snake_case; enums use names; "
            "persistent node IDs are scene-local strings.",
            {{"settings", schema::control_json::describe(
                              static_cast<const schema::CharacterControllerDataT*>(nullptr))},
             {"enabled", {{"type", "boolean"}}}},
            {"settings"}) ||
        !mcp::tools::component_tool(system, editor, "get_character_controller",
                                    "Read the complete generated settings.", Json::object()))
    {
        return false;
    }
    return mcp::tools::component_tool(system, editor, "get_character_status",
                                      "Read the last completed motor snapshot, sampled actions, "
                                      "abilities and event sequences.",
                                      Json::object()) &&
           mcp::tools::component_tool(
               system, editor, "set_character_input",
               "Submit one transient input sample to this character; missing next-frame input "
               "becomes zero. No undo or scene save.",
               {{"input", detail::input_schema()}}, {"input"}) &&
           mcp::tools::component_tool(system, editor, "clear_character_input",
                                      "Cancel this character's active intent and buffered actions, "
                                      "preserving external velocity.",
                                      Json::object()) &&
           register_character_camera_mcp_tools(system, editor) &&
           register_character_visual_mcp_tools(system, editor);
}
} // namespace engine::plugins::character_controller
