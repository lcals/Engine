#include "plugins/editor/scene_bake/scene_bake_mcp_tools.hpp"

#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#include "plugins/editor/mcp/mcp_system.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace engine::plugins::scene_bake
{
SceneBakeMcpTools::SceneBakeMcpTools(mcp::EditorMcpTools& editor) noexcept : editor_(editor)
{
}
bool SceneBakeMcpTools::register_tools(mcp::McpSystem& system)
{
    using Json = nlohmann::json;
    const auto object = [](Json properties, Json required = Json::array())
    {
        return Json{{"type", "object"},
                    {"properties", std::move(properties)},
                    {"required", std::move(required)},
                    {"additionalProperties", false}};
    };
    const auto add = [&](std::string name, std::string description, Json schema)
    {
        const auto command = name;
        return system.register_tool({std::move(name), std::move(description), std::move(schema),
                                     [this, command](const Json& arguments, Json& reply)
                                     { return editor_.call_command(command, arguments, reply); }});
    };
    const Json path = {{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}};
    const Json job = {{"type", "string"}, {"minLength", 1}, {"maxLength", 64}};
    return add("bake_scene", "异步烘焙已保存预览场景的 IBL；仅完成后发布资源，保留效果开关。",
               object({{"scene", path},
                       {"environment", path},
                       {"effects",
                        {{"type", "array"},
                         {"items", {{"type", "string"}, {"enum", {"ibl"}}}},
                         {"minItems", 1},
                         {"maxItems", 1}}},
                       {"resolution", {{"type", "integer"}, {"minimum", 16}, {"maximum", 512}}},
                       {"samples", {{"type", "integer"}, {"minimum", 16}, {"maximum", 1024}}},
                       {"lutSize", {{"type", "integer"}, {"minimum", 32}, {"maximum", 512}}}},
                      {"scene", "environment", "effects"})) &&
           add("get_scene_bake", "查询 GPU 烘焙状态；complete 表示资源与场景已发布并刷新。",
               object({{"job", job}})) &&
           add("cancel_scene_bake", "取消当前烘焙；在途 GPU 读回完成后释放，不发布部分资源。",
               object({{"job", job}}, {"job"}));
}
} // namespace engine::plugins::scene_bake
