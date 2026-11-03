#pragma once

namespace engine::plugins::mcp
{
class EditorMcpTools;
class McpSystem;
} // namespace engine::plugins::mcp

namespace engine::plugins::modeling
{

// 登记回调直接借用 editor；它须存活到 McpSystem 关闭。
[[nodiscard]] bool register_modeling_tools(engine::plugins::mcp::McpSystem& system,
                                           engine::plugins::mcp::EditorMcpTools& editor);

} // namespace engine::plugins::modeling
