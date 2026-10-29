#pragma once

namespace engine::plugins::mcp
{
class McpSystem;
class EditorMcpTools;
} // namespace engine::plugins::mcp

namespace engine::plugins::character_controller
{

// 仅协议适配，editor 须覆盖所有登记回调的生命周期。
[[nodiscard]] bool register_controller_mcp_tools(mcp::McpSystem& system,
                                                 mcp::EditorMcpTools& editor);

} // namespace engine::plugins::character_controller
