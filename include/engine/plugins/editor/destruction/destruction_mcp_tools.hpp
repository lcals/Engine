#pragma once

namespace engine::plugins::mcp
{
class McpSystem;
class EditorMcpTools;
} // namespace engine::plugins::mcp

namespace engine::plugins::destruction
{

[[nodiscard]] bool register_destruction_mcp_tools(mcp::McpSystem& system,
                                                  mcp::EditorMcpTools& editor);

} // namespace engine::plugins::destruction
