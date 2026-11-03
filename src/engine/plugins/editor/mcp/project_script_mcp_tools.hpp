#pragma once

#include "plugins/editor/mcp/mcp_target.hpp"

#include <filesystem>

namespace engine::plugins::mcp
{
class McpSystem;
class EditorMcpTools;

[[nodiscard]] bool register_project_script_tools(McpSystem& system, EditorMcpTools& editor,
                                                const std::filesystem::path& project_root,
                                                const McpTarget& target);
} // namespace engine::plugins::mcp
