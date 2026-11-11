#pragma once
namespace engine::plugins::mcp
{
class McpSystem;
class EditorMcpTools;
} // namespace engine::plugins::mcp
namespace engine::plugins::camera_editor
{
[[nodiscard]] bool register_camera_mcp_tools(mcp::McpSystem&, mcp::EditorMcpTools&);
}
