#pragma once

namespace engine::plugins::mcp
{
class EditorMcpTools;
class McpSystem;
} // namespace engine::plugins::mcp
namespace engine::plugins::ui
{
// 登记回调直接借用 editor；它须存活到 McpSystem 关闭。
[[nodiscard]] bool register_ui_editor_tools(mcp::McpSystem& system, mcp::EditorMcpTools& editor);
} // namespace engine::plugins::ui
