#pragma once

namespace engine::plugins::mcp
{
class EditorMcpTools;
class McpSystem;

// 登记回调直接借用 editor；它须存活到 McpSystem 关闭。
[[nodiscard]] bool register_render_tools(McpSystem& system, EditorMcpTools& editor);
} // namespace engine::plugins::mcp
