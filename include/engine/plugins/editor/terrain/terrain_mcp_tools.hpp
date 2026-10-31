#pragma once

namespace engine::plugins::mcp
{
class McpSystem;
class EditorMcpTools;
} // namespace engine::plugins::mcp

namespace engine::plugins::terrain
{

[[nodiscard]] bool register_terrain_mcp_tools(mcp::McpSystem& system, mcp::EditorMcpTools& editor);

} // namespace engine::plugins::terrain
