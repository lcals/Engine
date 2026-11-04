#pragma once

namespace engine::plugins::mcp
{
class McpSystem;
class EditorMcpTools;
} // namespace engine::plugins::mcp

namespace engine::plugins::animation_tools
{
[[nodiscard]] bool register_animation_authoring_mcp_tools(mcp::McpSystem& system,
                                                          mcp::EditorMcpTools& editor);
} // namespace engine::plugins::animation_tools
