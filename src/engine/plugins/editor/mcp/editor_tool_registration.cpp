#include "plugins/editor/mcp/editor_tool_registration.hpp"

#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#include "plugins/editor/mcp/mcp_system.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace engine::plugins::mcp
{
bool register_editor_command_tool(McpSystem& system, EditorMcpTools& editor, std::string name,
                                  std::string description, nlohmann::json input_schema,
                                  std::string command)
{
    if (command.empty())
    {
        return false;
    }
    return system.register_tool({std::move(name), std::move(description), std::move(input_schema),
                                 [&editor, command = std::move(command)](
                                     const nlohmann::json& arguments, nlohmann::json& reply)
                                 { return editor.call_command(command, arguments, reply); }});
}

nlohmann::json object_schema(nlohmann::json properties, nlohmann::json required)
{
    return {{"type", "object"},
            {"properties", std::move(properties)},
            {"required", std::move(required)},
            {"additionalProperties", false}};
}
} // namespace engine::plugins::mcp
