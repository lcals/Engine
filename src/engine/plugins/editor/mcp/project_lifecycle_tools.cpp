#include "plugins/editor/mcp/component_mcp_tools.hpp"

namespace engine::plugins::mcp
{
bool register_project_lifecycle_tools(McpSystem& system, EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    for (const auto& [name, description] :
         {std::pair{"start_project", "Start the loaded project in the existing editor window."},
          {"stop_project",
           "Stop the project, release its resources, and restore the blank editor window."}})
    {
        const std::string command = name;
        if (!system.register_tool({command,
                                   description,
                                   {{"type", "object"},
                                    {"properties", Json::object()},
                                    {"additionalProperties", false}},
                                   [&editor, command](const Json& arguments, Json& output)
                                   { return editor.call_command(command, arguments, output); }}))
        {
            return false;
        }
    }
    return true;
}
} // namespace engine::plugins::mcp
