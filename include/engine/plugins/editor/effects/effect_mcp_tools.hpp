#pragma once

namespace engine::plugins::mcp
{
class McpSystem;
class EditorMcpTools;
} // namespace engine::plugins::mcp

namespace engine::plugins::effects
{

[[nodiscard]] bool register_effect_mcp_tools(mcp::McpSystem& system, mcp::EditorMcpTools& editor);

} // namespace engine::plugins::effects
