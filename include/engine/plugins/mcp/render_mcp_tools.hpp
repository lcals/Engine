#pragma once

namespace engine
{
class EditorMcpTools;
class McpSystem;

class RenderMcpTools final
{
public:
    explicit RenderMcpTools(EditorMcpTools& editor) noexcept;
    [[nodiscard]] bool register_tools(McpSystem& system);

private:
    EditorMcpTools& editor_;
};
} // namespace engine
