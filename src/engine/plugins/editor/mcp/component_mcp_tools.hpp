#pragma once
#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#include "plugins/editor/mcp/mcp_system.hpp"
#include <cstdio>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
namespace engine::plugins::mcp
{
[[nodiscard]] bool register_physics_tools(McpSystem& system, EditorMcpTools& editor);
[[nodiscard]] bool register_audio_tools(McpSystem& system, EditorMcpTools& editor);
[[nodiscard]] bool register_animation_tools(McpSystem& system, EditorMcpTools& editor);
[[nodiscard]] bool register_model_tools(McpSystem& system, EditorMcpTools& editor);
namespace tools
{
inline nlohmann::json component_schema(nlohmann::json properties,
                                       nlohmann::json required = nlohmann::json::array())
{
    properties["sceneId"] = {{"type", "string"}, {"minLength", 1}};
    properties["nodeId"] = {{"type", "string"}, {"minLength", 1}};
    required.push_back("sceneId");
    required.push_back("nodeId");
    return {{"type", "object"},
            {"properties", std::move(properties)},
            {"required", std::move(required)},
            {"additionalProperties", false}};
}
inline bool component_tool(McpSystem& system, EditorMcpTools& editor, std::string name,
                           std::string description, nlohmann::json properties,
                           nlohmann::json required = nlohmann::json::array())
{
    const auto command = name;
    const bool registered = system.register_tool(
        {std::move(name), std::move(description),
         component_schema(std::move(properties), std::move(required)),
         [&editor, command](const nlohmann::json& arguments, nlohmann::json& reply)
         { return editor.call_command(command, arguments, reply); }});
    if (!registered)
    {
        std::fprintf(stderr, "Cannot register component MCP tool: %s\n", command.c_str());
    }
    return registered;
}
} // namespace tools
} // namespace engine::plugins::mcp
