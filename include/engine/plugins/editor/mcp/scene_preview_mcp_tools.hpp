#pragma once

namespace engine::plugins::mcp
{
class EditorMcpTools;
class McpSystem;

class ScenePreviewMcpTools final
{
public:
    // editor 与本对象须存活到登记工具的 McpSystem 关闭。
    explicit ScenePreviewMcpTools(EditorMcpTools& editor) noexcept;
    [[nodiscard]] bool register_tools(McpSystem& system);

private:
    EditorMcpTools& editor_;
};
} // namespace engine::plugins::mcp
