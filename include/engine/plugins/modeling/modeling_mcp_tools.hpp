#pragma once

namespace engine
{

class EditorMcpTools;
class McpSystem;

class ModelingMcpTools final
{
public:
    explicit ModelingMcpTools(EditorMcpTools& editor) noexcept;
    // 两个对象均须存活到 McpSystem 关闭。
    [[nodiscard]] bool register_tools(McpSystem& system);

private:
    EditorMcpTools& editor_;
};

} // namespace engine
