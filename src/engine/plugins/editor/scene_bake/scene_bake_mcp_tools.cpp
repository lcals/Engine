#include "plugins/editor/scene_bake/scene_bake_mcp_tools.hpp"
#include "plugins/editor/mcp/editor_tool_registration.hpp"

#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#include "plugins/runtime/mcp/mcp_system.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace engine::plugins::scene_bake
{
bool register_scene_bake_tools(mcp::McpSystem& system, mcp::EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    const auto add = [&](std::string name, std::string description, Json schema)
    {
        const auto command = name;
        return mcp::register_editor_command_tool(
            system, editor, std::move(name), std::move(description), std::move(schema), command);
    };
    const Json path = {{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}};
    const Json job = {{"type", "string"}, {"minLength", 1}, {"maxLength", 64}};
    const auto vector = [](Json item)
    {
        return Json{
            {"type", "array"}, {"items", std::move(item)}, {"minItems", 3}, {"maxItems", 3}};
    };
    const Json node_id = {{"type", "integer"}, {"minimum", 1}};
    const auto probe = mcp::object_schema(
        {{"nodeId", node_id},
         {"boundsMin", vector({{"type", "number"}, {"minimum", -1000000}, {"maximum", 1000000}})},
         {"boundsMax", vector({{"type", "number"}, {"minimum", -1000000}, {"maximum", 1000000}})},
         {"grid", vector({{"type", "integer"}, {"minimum", 2}, {"maximum", 64}})}},
        {"nodeId", "boundsMin", "boundsMax", "grid"});
    return add("bake_scene",
               "Asynchronously bake IBL or multibounce Lightmaps/SH9 probes for fixed geometry; "
               "publish only on full success. lighting "
               "uses bake_static tags; lightNodeIds selects fixed lights explicitly.",
               mcp::object_schema(
                   {{"scene", path},
                    {"environment", path},
                    {"effects",
                     {{"type", "array"},
                      {"items", {{"type", "string"}, {"enum", {"ibl", "lighting"}}}},
                      {"minItems", 1},
                      {"maxItems", 1}}},
                    {"resolution", {{"type", "integer"}, {"minimum", 16}, {"maximum", 512}}},
                    {"samples", {{"type", "integer"}, {"minimum", 16}, {"maximum", 1024}}},
                    {"lutSize", {{"type", "integer"}, {"minimum", 32}, {"maximum", 512}}},
                    {"hemisphereResolution",
                     {{"type", "integer"}, {"minimum", 16}, {"maximum", 128}}},
                    {"bounces", {{"type", "integer"}, {"minimum", 1}, {"maximum", 8}}},
                    {"interpolationPasses", {{"type", "integer"}, {"minimum", 0}, {"maximum", 4}}},
                    {"interpolationThreshold",
                     {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                    {"lightNodeIds", {{"type", "array"}, {"items", node_id}, {"maxItems", 1024}}},
                    {"probeVolumes", {{"type", "array"}, {"items", probe}, {"maxItems", 64}}}},
                   {"scene", "effects"})) &&
           add("get_scene_bake",
               "Query GPU bake status; complete means resources and the scene have been published "
               "and refreshed.",
               mcp::object_schema({{"job", job}}, Json::array())) &&
           add("cancel_scene_bake",
               "Cancel the bake; release resources after in-flight GPU readback completes, without "
               "publishing partial output.",
               mcp::object_schema({{"job", job}}, {"job"}));
}
} // namespace engine::plugins::scene_bake
