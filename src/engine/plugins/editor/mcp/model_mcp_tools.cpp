#include "plugins/editor/mcp/component_mcp_tools.hpp"

namespace engine::plugins::mcp
{
bool register_model_tools(McpSystem& system, EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    const Json schema = {{"type", "object"},
                         {"properties",
                          {{"sceneId", {{"type", "string"}, {"minLength", 1}}},
                           {"path", {{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}},
                           {"name", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}}},
                         {"required", {"sceneId", "path", "name"}},
                         {"additionalProperties", false}};
    return system.register_tool(
        {"instantiate_model",
         "Instantiate a model package by resource path into ordinary scene nodes. Undoable; "
         "failure leaves existing nodes intact. Use get_scene to inspect created node IDs.",
         schema, [&editor](const Json& arguments, Json& reply)
         { return editor.call_command("instantiate_model", arguments, reply); }});
}
} // namespace engine::plugins::mcp
