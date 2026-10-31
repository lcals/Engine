#pragma once

namespace engine::plugins::mcp
{
class McpSystem;
class EditorMcpTools;
} // namespace engine::plugins::mcp

namespace engine::plugins::character_controller
{

[[nodiscard]] bool register_character_camera_mcp_tools(mcp::McpSystem& system,
                                                       mcp::EditorMcpTools& editor);

} // namespace engine::plugins::character_controller
