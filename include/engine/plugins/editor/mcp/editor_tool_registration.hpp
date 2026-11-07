#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace engine::plugins::mcp
{
class McpSystem;
class EditorMcpTools;

// 登记回调持续借用 editor；通道须存活到 system 清空工具表。
[[nodiscard]] bool register_editor_command_tool(McpSystem& system, EditorMcpTools& editor,
                                                std::string name, std::string description,
                                                nlohmann::json input_schema, std::string command);
[[nodiscard]] nlohmann::json object_schema(nlohmann::json properties, nlohmann::json required);
} // namespace engine::plugins::mcp
