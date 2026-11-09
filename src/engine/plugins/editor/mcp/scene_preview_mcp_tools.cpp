#include "plugins/editor/mcp/scene_preview_mcp_tools.hpp"
#include "plugins/editor/mcp/editor_tool_registration.hpp"

#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#include "plugins/editor/mcp/mcp_system.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace engine::plugins::mcp
{

bool register_scene_preview_tools(McpSystem& system, EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    const Json vector = {{"type", "array"},
                         {"items", {{"type", "number"}}},
                         {"minItems", 3},
                         {"maxItems", 3}};
    const Json camera =
        object_schema({{"position", vector},
                       {"rotation", vector},
                       {"speed", {{"type", "number"}, {"minimum", 0.01}, {"maximum", 1000}}}},
                      Json::array());
    const auto add = [&](std::string name, std::string description, Json schema)
    {
        const auto command = name;
        return register_editor_command_tool(system, editor, std::move(name), std::move(description),
                                            std::move(schema), command);
    };
    return add("open_scene_preview",
               "Preview a scene resource in the editor without starting gameplay; replaces the "
               "current scene and discards unsaved edits."
               " Camera position, rotation and movement speed can be specified independently.",
               object_schema({{"path", {{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}}},
                              {"camera", camera}},
                             {"path"})) &&
           add("close_scene_preview",
               "Close the scene preview, discard unsaved edits and release resources, restoring an "
               "empty window.",
               object_schema(Json::object(), Json::array())) &&
           add("get_scene_preview",
               "Query preview, camera, unsaved edit and pending refresh status.",
               object_schema(Json::object(), Json::array())) &&
           add("set_preview_camera",
               "Change the preview camera without saving it to the scene; provide at least one of "
               "position, rotation or speed."
               " Angles use degrees; speed is 0.01..1000 meters per second.",
               camera) &&
           add("refresh_scene_preview",
               "Refresh the preview after incremental asset import; discardChanges=true is "
               "required to discard unsaved edits.",
               object_schema({{"discardChanges", {{"type", "boolean"}}}}, Json::array()));
}
} // namespace engine::plugins::mcp
