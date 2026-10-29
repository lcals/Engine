#pragma once

namespace engine::plugins::mcp
{
class EditorMcpTools;
class McpSystem;
} // namespace engine::plugins::mcp

namespace engine::plugins::modeling
{

class ModelingMcpTools final
{
public:
    explicit ModelingMcpTools(engine::plugins::mcp::EditorMcpTools& editor) noexcept;
    // 两个对象均须存活到 McpSystem 关闭。
    [[nodiscard]] bool register_tools(engine::plugins::mcp::McpSystem& system);

private:
    engine::plugins::mcp::EditorMcpTools& editor_;
};

} // namespace engine::plugins::modeling
