#include "plugins/editor/destruction/destruction_mcp_tools.hpp"

#include "plugins/editor/mcp/component_mcp_tools.hpp"

namespace engine::plugins::destruction
{

bool register_destruction_mcp_tools(mcp::McpSystem& system, mcp::EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    if (!system.register_tool(
            {"create_destruction_asset",
             "Create a validated .destruction TOML source and unique meta under assets. "
             "Supports size, fracture (slices/voronoi), slices_x/y/z (piece counts), sites, seed, "
             "bond_health and anchor_height. Uses official Blast Authoring. Does not overwrite "
             "files. Import afterwards using refresh_scene_preview or sync_assets.",
             {{"type", "object"},
              {"properties",
               {{"path", {{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}},
                {"source", {{"type", "string"}, {"minLength", 1}, {"maxLength", 1048576}}}}},
              {"required", Json::array({"path", "source"})},
              {"additionalProperties", false}},
             [&editor](const Json& arguments, Json& reply)
             { return editor.call_command("create_destruction_asset", arguments, reply); }}))
    {
        return false;
    }
    return mcp::tools::component_tool(
               system, editor, "set_destruction",
               "Create or edit a destruction component. destruction and material are resource "
               "paths; an empty destruction clears the object. Undoable with complete state.",
               {{"enabled", {{"type", "boolean"}}},
                {"destruction", {{"type", "string"}}},
                {"material", {{"type", "string"}}},
                {"density", {{"type", "number"}, {"minimum", 0}, {"maximum", 100000}}},
                {"friction", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                {"restitution", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}}}) &&
           mcp::tools::component_tool(
               system, editor, "get_destruction",
               "Read destruction settings, readiness, revision, chunks, groups and dynamic groups.",
               Json::object()) &&
           mcp::tools::component_tool(
               system, editor, "destruction_damage",
               "Apply a world-space spherical damage region and optional outward impulse. "
               "Synchronously updates Blast groups and Jolt collision. Undoable; runtime damage "
               "is separate from the source asset and scene settings.",
               {{"center",
                 {{"type", "array"},
                  {"items", {{"type", "number"}}},
                  {"minItems", 3},
                  {"maxItems", 3}}},
                {"radius", {{"type", "number"}, {"minimum", 0}}},
                {"damage", {{"type", "number"}, {"minimum", 0}}},
                {"impulse", {{"type", "number"}, {"minimum", 0}}}},
               Json::array({"center", "radius", "damage"})) &&
           mcp::tools::component_tool(
               system, editor, "destruction_refresh_support",
               "Check multi-sample foundation coverage against one static collider after terrain "
               "collision has been committed. Release unsupported world bonds via Blast. Undoable; "
               "does not reattach previously released anchors.",
               {{"supportNodeId", {{"type", "string"}}},
                {"maxGap", {{"type", "number"}, {"minimum", 0}, {"maximum", 1000}}},
                {"minCoverage", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}}},
               Json::array({"supportNodeId", "maxGap", "minCoverage"})) &&
           mcp::tools::component_tool(system, editor, "destruction_reset",
                                      "Restore the original destruction structure. Undoable.",
                                      Json::object());
}

} // namespace engine::plugins::destruction
