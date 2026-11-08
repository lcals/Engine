#include "plugins/editor/scene_bake/scene_bake_mcp_tools.hpp"
#include "plugins/editor/mcp/editor_tool_registration.hpp"

#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#include "plugins/editor/mcp/mcp_system.hpp"

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
               "异步烘焙 IBL 或固定几何的多次反弹 Lightmap/SH9 探针；仅完整成功后发布。lighting "
               "使用 bake_static 标记，固定灯通过 lightNodeIds 白名单选择。",
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
           add("get_scene_bake", "查询 GPU 烘焙状态；complete 表示资源与场景已发布并刷新。",
               mcp::object_schema({{"job", job}}, Json::array())) &&
           add("cancel_scene_bake", "取消当前烘焙；在途 GPU 读回完成后释放，不发布部分资源。",
               mcp::object_schema({{"job", job}}, {"job"}));
}
} // namespace engine::plugins::scene_bake
