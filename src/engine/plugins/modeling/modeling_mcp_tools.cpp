#include "plugins/modeling/modeling_mcp_tools.hpp"

#include "plugins/mcp/editor_mcp_tools.hpp"
#include "plugins/mcp/mcp_system.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <string>
#include <utility>

namespace engine
{
namespace
{
using Json = nlohmann::json;

Json object(Json properties, Json required = Json::array())
{
    return {{"type", "object"},
            {"properties", std::move(properties)},
            {"required", std::move(required)},
            {"additionalProperties", false}};
}

Json id()
{
    return {{"type", "string"}, {"minLength", 1}, {"maxLength", 20}};
}

Json selection()
{
    const Json ids = {{"type", "array"}, {"items", id()}, {"maxItems", 100000}};
    return object(
        {{"vertices", ids},
         {"faces", ids},
         {"edges",
          {{"type", "array"},
           {"items", {{"type", "array"}, {"items", id()}, {"minItems", 2}, {"maxItems", 2}}},
           {"maxItems", 100000}}},
         {"all", {{"type", "boolean"}}}});
}
} // namespace

ModelingMcpTools::ModelingMcpTools(EditorMcpTools& editor) noexcept : editor_(editor)
{
}

bool ModelingMcpTools::register_tools(McpSystem& system)
{
    const auto add = [&](std::string name, std::string description, Json schema)
    {
        const auto command = name;
        const bool registered =
            system.register_tool({std::move(name), std::move(description), std::move(schema),
                                  [this, command](const Json& arguments, Json& reply)
                                  { return editor_.call_command(command, arguments, reply); }});
        if (!registered)
        {
            std::fprintf(stderr, "Cannot register modeling MCP tool: %s\n", command.c_str());
        }
        return registered;
    };
    const Json target = {{"sceneId", id()}, {"modelId", id()}, {"nodeId", id()}};
    const Json required = {"sceneId", "modelId"};
    auto create = Json{
        {"sceneId", id()},
        {"name", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
        {"shape",
         {{"type", "string"},
          {"enum",
           {"cube", "plane", "cylinder", "cone", "sphere", "torus", "prism", "stairs", "arch",
            "pipe", "door", "curved_stairs", "sprite", "icosphere", "polygon", "sweep", "lathe"}}}},
        {"parameters", {{"type", "object"}}}};
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
    return add("modeling_help", "发现全部建模操作的参数契约、示例和限制；无需启动项目。",
               object({{"operation", {{"type", "string"}}}})) &&
           add("modeling_create", "创建可编辑模型；返回稳定 modelId，再查询当前 nodeId。",
               object(create, {"sceneId", "shape"})) &&
           add("modeling_get", "查询模型与分页拓扑；省略 modelId 列出全部模型。",
               object(get, {"sceneId"})) &&
           add("modeling_select", "设置顶点、边或面选择；选择支持撤销和批量事务。",
               object(select, {"sceneId", "modelId"})) &&
           add("modeling_edit", "对可编辑模型执行 C++ 几何操作；失败保持源数据与场景不变。",
               object(edit, {"sceneId", "modelId", "operation"})) &&
           add("modeling_delete", "删除模型；撤销保留 modelId，恢复节点的 nodeId 会改变。",
               object(target, required)) &&
           add("modeling_duplicate", "复制可编辑模型与当前变换、材质。",
               object(duplicate, required)) &&
           add("modeling_combine", "按场景变换合并模型为新对象，原对象保持不变。",
               object(combine, {"sceneId", "modelIds"})) &&
           add("modeling_separate", "把选择的面分离到新模型；整次修改可撤销。",
               object(separate, required)) &&
           add("modeling_save", "将模型保存为 assets 内 .mesh 源资源，meta 由导入管线维护。",
               object(save, {"sceneId", "modelId", "path"})) &&
           add("modeling_load", "读取 assets 内含建模源数据的 .mesh 并创建可编辑模型。",
               object(load, {"sceneId", "path"}));
}

} // namespace engine
