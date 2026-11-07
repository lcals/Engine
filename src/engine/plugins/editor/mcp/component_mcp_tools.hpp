#pragma once

#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#include "plugins/editor/mcp/editor_tool_registration.hpp"
#include "plugins/editor/mcp/mcp_system.hpp"
#include <nlohmann/json.hpp>
namespace engine::plugins::mcp
{
[[nodiscard]] bool register_project_lifecycle_tools(McpSystem& system, EditorMcpTools& editor);
[[nodiscard]] bool register_physics_tools(McpSystem& system, EditorMcpTools& editor);
[[nodiscard]] bool register_audio_tools(McpSystem& system, EditorMcpTools& editor);
[[nodiscard]] bool register_animation_tools(McpSystem& system, EditorMcpTools& editor);
[[nodiscard]] bool register_model_tools(McpSystem& system, EditorMcpTools& editor);
[[nodiscard]] bool register_prefab_tools(McpSystem& system, EditorMcpTools& editor);

} // namespace engine::plugins::mcp
