#pragma once

namespace engine::plugins::mcp
{
class EditorMcpTools;
class McpSystem;
} // namespace engine::plugins::mcp
namespace engine::plugins::ui
{
class UiEditorMcpTools final
{
public:
    explicit UiEditorMcpTools(mcp::EditorMcpTools& editor) noexcept;
    ~UiEditorMcpTools() noexcept = default;
    UiEditorMcpTools(const UiEditorMcpTools&) = delete;
    UiEditorMcpTools& operator=(const UiEditorMcpTools&) = delete;
    UiEditorMcpTools(UiEditorMcpTools&&) = delete;
    UiEditorMcpTools& operator=(UiEditorMcpTools&&) = delete;
    [[nodiscard]] bool register_tools(mcp::McpSystem& system);

private:
    mcp::EditorMcpTools& editor_;
};
} // namespace engine::plugins::ui
