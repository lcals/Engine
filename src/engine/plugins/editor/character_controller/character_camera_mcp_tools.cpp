#include "plugins/editor/character_controller/character_camera_mcp_tools.hpp"
#include "plugins/editor/mcp/editor_tool_registration.hpp"
#include "runtime/schema/control_json_generated.hpp"
namespace engine::plugins::character_controller
{
bool register_character_camera_mcp_tools(mcp::McpSystem& system, mcp::EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    if (!mcp::tools::component_tool(
            system, editor, "set_character_camera",
            "Replace the complete generated CharacterCameraData settings; validated and undoable. "
            "Field names use the runtime schema's snake_case; enums use names; persistent node IDs "
            "are scene-local strings.",
            {{"settings", schema::control_json::describe(
                              static_cast<const schema::CharacterCameraDataT*>(nullptr))},
             {"enabled", {{"type", "boolean"}}}},
            {"settings"}) ||
        !mcp::tools::component_tool(system, editor, "get_character_camera",
                                    "Read the complete generated settings.", Json::object()))
    {
        return false;
    }
    return true;
}
} // namespace engine::plugins::character_controller
