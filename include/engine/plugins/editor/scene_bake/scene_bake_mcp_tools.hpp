#pragma once

namespace engine::plugins::mcp
{
class EditorMcpTools;
class McpSystem;
} // namespace engine::plugins::mcp
namespace engine::plugins::scene_bake
{
class SceneBakeMcpTools final
{
public:
    explicit SceneBakeMcpTools(mcp::EditorMcpTools& editor) noexcept;
    [[nodiscard]] bool register_tools(mcp::McpSystem& system);

private:
    mcp::EditorMcpTools& editor_;
};
} // namespace engine::plugins::scene_bake
