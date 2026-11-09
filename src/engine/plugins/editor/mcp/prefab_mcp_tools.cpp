#include "plugins/editor/mcp/component_mcp_tools.hpp"
#include "plugins/editor/mcp/editor_tool_registration.hpp"

namespace engine::plugins::mcp
{

bool register_prefab_tools(McpSystem& system, EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    const Json path = {{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}};
    const Json identity = {{"type", "string"}, {"minLength", 1}};
    const auto add = [&system, &editor](std::string name, std::string description, Json properties,
                                        Json required)
    {
        const auto command = name;
        return register_editor_command_tool(
            system, editor, std::move(name), std::move(description),
            object_schema(std::move(properties), std::move(required)), command);
    };
    const Json fields = {{"type", "array"},
                         {"items", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                         {"minItems", 1}};
    const Json override_properties = {
        {"fields", fields},
        {"componentType", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}};
    return add("instantiate_prefab",
               "Instantiate an independent prefab node tree and retain its source; supports undo.",
               {{"sceneId", identity},
                {"path", path},
                {"name", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}},
               {"sceneId", "path", "name"}) &&
           add("create_prefab_from_model",
               "Create a linked prefab from an imported Model; model is a resource path and path "
               "is a new .prefab path.",
               {{"model", path}, {"path", path}}, {"model", "path"}) &&
           tools::component_tool(system, editor, "create_prefab",
                                 "Save the selected subtree as an independent prefab; keep the "
                                 "original nodes and the scene's unsaved state.",
                                 {{"path", path}}, {"path"}) &&
           tools::component_tool(
               system, editor, "get_prefab_instance",
               "Query the prefab source, source node identity and explicit overrides of a node.",
               Json::object()) &&
           tools::component_tool(system, editor, "set_prefab_override",
                                 "Register current field values as instance overrides, including "
                                 "values equal to the template; omit componentType"
                                 " to edit node state.",
                                 override_properties, {"fields"}) &&
           tools::component_tool(
               system, editor, "revert_prefab_override",
               "Clear an override and restore the latest template value; componentType is the "
               "generated component type name. Supports undo.",
               override_properties, {"fields"}) &&
           tools::component_tool(system, editor, "link_prefab",
                                 "Link an unchanged exported source subtree to its template; "
                                 "stable node identities are required. Supports undo.",
                                 {{"path", path}}, {"path"}) &&
           tools::component_tool(system, editor, "unlink_prefab",
                                 "Unlink an instance root from its template and keep its nodes; "
                                 "later template changes no longer propagate. Supports undo.",
                                 Json::object());
}

} // namespace engine::plugins::mcp
