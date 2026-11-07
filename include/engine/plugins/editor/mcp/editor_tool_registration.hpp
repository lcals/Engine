#pragma once

#include <nlohmann/json.hpp>

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

namespace tools
{
[[nodiscard]] nlohmann::json component_schema(nlohmann::json properties,
                                              nlohmann::json required = nlohmann::json::array());
[[nodiscard]] bool component_tool(McpSystem& system, EditorMcpTools& editor, std::string name,
                                  std::string description, nlohmann::json properties,
                                  nlohmann::json required = nlohmann::json::array());
} // namespace tools
} // namespace engine::plugins::mcp
