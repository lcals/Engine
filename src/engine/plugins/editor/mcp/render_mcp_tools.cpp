#include "plugins/editor/mcp/render_mcp_tools.hpp"

#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#include "plugins/editor/mcp/mcp_system.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace engine::plugins::mcp
{
RenderMcpTools::RenderMcpTools(EditorMcpTools& editor) noexcept : editor_(editor)
{
}

bool RenderMcpTools::register_tools(McpSystem& system)
{
    using Json = nlohmann::json;
    const auto object = [](Json properties, Json required = Json::array())
    {
        return Json{{"type", "object"},
                    {"properties", std::move(properties)},
                    {"required", std::move(required)},
                    {"additionalProperties", false}};
    };
    const auto color = [](int count)
    {
        return Json{{"type", "array"},
                    {"items", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                    {"minItems", count},
                    {"maxItems", count}};
    };
    const auto add = [&](std::string name, std::string description, Json schema)
    {
        const auto command = name;
        return system.register_tool({std::move(name), std::move(description), std::move(schema),
                                     [this, command](const Json& arguments, Json& reply)
                                     { return editor_.call_command(command, arguments, reply); }});
    };
    return add("get_render_settings", "读取当前环境光、曝光、阴影与背景设置。",
               object(Json::object())) &&
           add("set_render_settings", "设置渲染效果；沿用编辑器事务和撤销。",
               object({{"ambientColor", color(3)},
                       {"ambientIntensity", {{"type", "number"}, {"minimum", 0}}},
                       {"exposure", {{"type", "number"}, {"minimum", 0.000001}}},
                       {"filmicTonemap", {{"type", "boolean"}}},
                       {"directionalShadows", {{"type", "boolean"}}},
                       {"shadowResolution",
                        {{"type", "integer"}, {"minimum", 256}, {"maximum", 8192}}},
                       {"shadowBias", {{"type", "number"}, {"minimum", 0}}},
                       {"clearColor", color(4)}})) &&
           add("capture_frame", "请求引擎真实渲染帧 PNG；path 相对于项目 cache，完成需查询。",
               object({{"path", {{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}}}},
                      {"path"})) &&
           add("get_frame_capture", "查询截图实际状态；complete 才表示文件写入完成。",
               object(Json::object()));
}
} // namespace engine::plugins::mcp
