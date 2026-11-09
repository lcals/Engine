#include "plugins/editor/modeling/modeling_mcp_tools.hpp"
#include "plugins/editor/mcp/editor_tool_registration.hpp"

#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#include "plugins/editor/mcp/mcp_system.hpp"
#include "plugins/editor/modeling/primitive_contract.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <string>
#include <utility>

namespace engine::plugins::modeling
{
namespace
{
using Json = nlohmann::json;

Json id()
{
    return {{"type", "string"}, {"minLength", 1}, {"maxLength", 20}};
}

Json selection()
{
    const Json ids = {{"type", "array"}, {"items", id()}, {"maxItems", 100000}};
    return mcp::object_schema(
        {{"vertices", ids},
         {"faces", ids},
         {"edges",
          {{"type", "array"},
           {"items", {{"type", "array"}, {"items", id()}, {"minItems", 2}, {"maxItems", 2}}},
           {"maxItems", 100000}}},
         {"all", {{"type", "boolean"}}}},
        Json::array());
}
} // namespace

bool register_modeling_tools(engine::plugins::mcp::McpSystem& system,
                             engine::plugins::mcp::EditorMcpTools& editor)
{
    const auto add = [&](std::string name, std::string description, Json schema)
    {
        const auto command = name;
        const bool registered = mcp::register_editor_command_tool(
            system, editor, std::move(name), std::move(description), std::move(schema), command);
        if (!registered)
        {
            std::fprintf(stderr, "Cannot register modeling MCP tool: %s\n", command.c_str());
        }
        return registered;
    };
    const Json target = {{"sceneId", id()}, {"modelId", id()}, {"nodeId", id()}};
    const Json required = {"sceneId", "modelId"};
    const auto contracts = detail::shape_contracts(false);
    Json shapes = Json::array();
    Json alternatives = Json::array();
    for (const auto& [shape, contract] : contracts.items())
    {
        shapes.push_back(shape);
        alternatives.push_back({{"type", "object"},
                                {"properties",
                                 {{"shape", {{"type", "string"}, {"enum", {shape}}}},
                                  {"parameters", contract["parameters"]}}},
                                {"required", {"shape"}}});
    }
    auto create =
        mcp::object_schema({{"sceneId", id()},
                            {"name", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                            {"shape", {{"type", "string"}, {"enum", shapes}}},
                            {"parameters", {{"type", "object"}}}},
                           {"sceneId", "shape"});
    create["oneOf"] = std::move(alternatives);
    auto rebuild = target;
    rebuild["parameters"] = {{"type", "object"}};
    auto get = target;
    get["offset"] = {{"type", "integer"}, {"minimum", 0}};
    get["limit"] = {{"type", "integer"}, {"minimum", 1}, {"maximum", 1024}};
    auto select = target;
    select["selection"] = selection();
    select["mode"] = {
        {"type", "string"},
        {"enum", {"replace", "grow", "shrink", "connected", "edge_ring", "edge_loop"}}};
    auto edit = target;
    edit["selection"] = selection();
    edit["operation"] = {{"type", "string"},
                         {"enum",
                          {"transform",
                           "extrude",
                           "extrude_edges",
                           "inset",
                           "erase_faces",
                           "reverse",
                           "triangulate",
                           "subdivide",
                           "weld",
                           "merge_vertices",
                           "split_vertices",
                           "detach",
                           "duplicate_faces",
                           "bridge",
                           "fill_hole",
                           "dissolve_edge",
                           "split_edge",
                           "loop_cut",
                           "connect_vertices",
                           "mirror",
                           "mirror_mode",
                           "clip",
                           "bevel",
                           "boolean",
                           "project_uv",
                           "transform_uv",
                           "unwrap_uv",
                           "unwrap_face_uv",
                           "hotspot_uv",
                           "smoothing",
                           "auto_smooth",
                           "material",
                           "color"}}};
    edit["parameters"] = {{"type", "object"}};
    auto duplicate = target;
    duplicate["name"] = {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}};
    auto combine =
        Json{{"sceneId", id()},
             {"modelIds", {{"type", "array"}, {"items", id()}, {"minItems", 2}, {"maxItems", 64}}},
             {"name", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}};
    auto separate = duplicate;
    separate["selection"] = selection();
    auto save = target;
    save["path"] = {{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}};
    const auto load = Json{{"sceneId", id()}, {"path", save["path"]}, {"name", duplicate["name"]}};
    return add("modeling_help",
               "Discover parameter contracts, examples and limits for all modeling operations; no "
               "running project is required.",
               mcp::object_schema({{"operation", {{"type", "string"}}},
                                   {"shape", {{"type", "string"}, {"enum", shapes}}}},
                                  Json::array())) &&
           add("modeling_create",
               "Create an editable model; returns a stable modelId. Query its current nodeId "
               "separately.",
               std::move(create)) &&
           add("modeling_rebuild",
               "Rebuild from saved parameters, resetting geometry, UVs and face edits while "
               "preserving the node and material bindings. Supports undo.",
               mcp::object_schema(rebuild, {"sceneId", "modelId", "parameters"})) &&
           add("modeling_get", "Query models and paged topology; omit modelId to list all models.",
               mcp::object_schema(get, {"sceneId"})) &&
           add("modeling_select",
               "Select vertices, edges or faces; supports undo and batch transactions.",
               mcp::object_schema(select, {"sceneId", "modelId"})) &&
           add("modeling_edit",
               "Apply a C++ geometry operation to an editable model; failure preserves source data "
               "and the scene.",
               mcp::object_schema(edit, {"sceneId", "modelId", "operation"})) &&
           add("modeling_delete",
               "Delete a model; undo preserves modelId, but the restored node receives a new "
               "nodeId.",
               mcp::object_schema(target, required)) &&
           add("modeling_duplicate",
               "Duplicate an editable model with its current transform and materials.",
               mcp::object_schema(duplicate, required)) &&
           add("modeling_combine",
               "Merge models using their scene transforms into a new object, preserving the "
               "originals.",
               mcp::object_schema(combine, {"sceneId", "modelIds"})) &&
           add("modeling_separate",
               "Detach selected faces into a new model; the entire change supports undo.",
               mcp::object_schema(separate, required)) &&
           add("modeling_save",
               "Save the model as a .mesh source under assets; the import pipeline manages its "
               "meta.",
               mcp::object_schema(save, {"sceneId", "modelId", "path"})) &&
           add("modeling_load",
               "Load modeling source data from a .mesh under assets and create an editable model.",
               mcp::object_schema(load, {"sceneId", "path"}));
}

} // namespace engine::plugins::modeling
