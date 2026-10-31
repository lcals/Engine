#include "plugins/editor/terrain/terrain_mcp_tools.hpp"

#include "plugins/editor/mcp/component_mcp_tools.hpp"

namespace engine::plugins::terrain
{

bool register_terrain_mcp_tools(mcp::McpSystem& system, mcp::EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    const auto vector = [](std::size_t size)
    {
        return Json{{"type", "array"},
                    {"items", {{"type", "number"}}},
                    {"minItems", size},
                    {"maxItems", size}};
    };
    const Json state_path = {{"path", {{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}}};
    if (!system.register_tool(
            {"create_terrain_asset",
             "Validate and create a terrain TOML source and unique meta under assets. path is "
             "relative to assets and must end in .terrain. Source supports cells_x/y/z, "
             "voxel_size, "
             "chunk_cells, origin, height, amplitude, frequency, seed and [[cavities]] "
             "center/radius. "
             "Does not overwrite existing files. Use refresh_scene_preview or sync_assets to "
             "import afterwards.",
             {{"type", "object"},
              {"properties",
               {{"path", {{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}},
                {"source", {{"type", "string"}, {"minLength", 1}, {"maxLength", 1048576}}}}},
              {"required", Json::array({"path", "source"})},
              {"additionalProperties", false}},
             [&editor](const Json& arguments, Json& reply)
             { return editor.call_command("create_terrain_asset", arguments, reply); }}))
    {
        return false;
    }
    return mcp::tools::component_tool(
               system, editor, "set_terrain",
               "Create or edit a terrain component. terrain and material are imported resource "
               "paths; empty terrain clears the volume. Component settings are undoable.",
               {{"enabled", {{"type", "boolean"}}},
                {"terrain", {{"type", "string"}}},
                {"material", {{"type", "string"}}},
                {"collision", {{"type", "boolean"}}},
                {"surfaceColor", vector(4)},
                {"wallColor", vector(4)},
                {"uvScale", {{"type", "number"}, {"minimum", 0}, {"maximum", 100}}}}) &&
           mcp::tools::component_tool(
               system, editor, "get_terrain",
               "Read terrain settings, readiness, revision, chunk, triangle and fragmentCount.",
               Json::object()) &&
           mcp::tools::component_tool(
               system, editor, "terrain_collapse_detached",
               "Convert unanchored terrain islands into dynamic soil fragments using a total "
               "active-fragment budget. Parameters select minimum island samples, cell-group size "
               "and density in kg/m3. Synchronous runtime operation, not undoable. Failed "
               "conversion keeps the existing terrain. Active fragments prevent volume-only "
               "save/undo; use terrain_reset_runtime to restore the original resource.",
               {{"minSamples", {{"type", "integer"}, {"minimum", 1}, {"maximum", 4194304}}},
                {"maxFragments", {{"type", "integer"}, {"minimum", 1}, {"maximum", 128}}},
                {"fragmentCells", {{"type", "integer"}, {"minimum", 1}, {"maximum", 8}}},
                {"density", {{"type", "number"}, {"minimum", 0}, {"maximum", 100000}}}},
               Json::array({"minSamples", "maxFragments"})) &&
           mcp::tools::component_tool(
               system, editor, "terrain_reset_runtime",
               "Restore original terrain and remove all dynamic soil fragments. Explicit runtime "
               "operation, not undoable. Use terrain_reset for undoable static-volume reset.",
               Json::object()) &&
           mcp::tools::component_tool(
               system, editor, "terrain_brush",
               "Synchronously dig or fill a sphere in terrain node-local coordinates, including "
               "the terrain resource origin. Updates display and collision together. Undoable; "
               "does not overwrite the original terrain resource.",
               {{"center", vector(3)},
                {"radius", {{"type", "number"}, {"minimum", 0}, {"maximum", 10000}}},
                {"operation", {{"type", "string"}, {"enum", Json::array({"dig", "fill"})}}}},
               Json::array({"center", "radius"})) &&
           mcp::tools::component_tool(system, editor, "terrain_reset",
                                      "Restore the original terrain volume; undoable.",
                                      Json::object()) &&
           mcp::tools::component_tool(
               system, editor, "terrain_save_state",
               "Save current terrain volume separately from source assets. path is relative to "
               "cache/terrain-states and must end in .terrainstate. Replaces that save file.",
               state_path, Json::array({"path"})) &&
           mcp::tools::component_tool(
               system, editor, "terrain_load_state",
               "Restore a terrain save from cache/terrain-states. Requires a relative "
               ".terrainstate path and matching base resource GUID/hash. Undoable.",
               state_path, Json::array({"path"}));
}

} // namespace engine::plugins::terrain
