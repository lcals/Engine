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
    return add("get_render_settings", "读取当前环境光、雾、曝光、阴影与背景设置。",
               object(Json::object())) &&
           add("set_render_settings",
               "编辑场景环境、相机后处理和方向光阴影，支持保存及撤销；无场景时修改待机默认。",
               object(
                   {{"ambientColor", color(3)},
                    {"ambientIntensity", {{"type", "number"}, {"minimum", 0}}},
                    {"iblEnabled", {{"type", "boolean"}}},
                    {"iblIntensity", {{"type", "number"}, {"minimum", 0}, {"maximum", 1000}}},
                    {"ambientTricolor", {{"type", "boolean"}}},
                    {"ambientSkyColor", color(3)},
                    {"ambientEquatorColor", color(3)},
                    {"ambientGroundColor", color(3)},
                    {"linearFog", {{"type", "boolean"}}},
                    {"fogMode",
                     {{"type", "string"},
                      {"enum", {"none", "linear", "exponential", "exponential_squared"}}}},
                    {"fogColor", color(3)},
                    {"fogStart", {{"type", "number"}, {"minimum", 0}}},
                    {"fogEnd", {{"type", "number"}, {"minimum", 0}}},
                    {"fogDensity", {{"type", "number"}, {"minimum", 0}}},
                    {"exposure", {{"type", "number"}, {"minimum", 0.000001}}},
                    {"filmicTonemap", {{"type", "boolean"}}},
                    {"aoEnabled", {{"type", "boolean"}}},
                    {"hdrEnabled", {{"type", "boolean"}}},
                    {"bloomEnabled",
                     {{"type", "boolean"}, {"description", "启用 Bloom 要求同时启用 HDR。"}}},
                    {"bloomThreshold",
                     {{"type", "number"}, {"minimum", 0}, {"maximum", 100}}},
                    {"bloomIntensity", {{"type", "number"}, {"minimum", 0}, {"maximum", 10}}},
                    {"bloomRadius", {{"type", "number"}, {"minimum", 0.25}, {"maximum", 4}}},
                    {"aoRadius",
                     {{"type", "number"},
                      {"minimum", 0},
                      {"maximum", 100},
                      {"description", "世界空间半径，必须大于 0。"}}},
                    {"aoStrength", {{"type", "number"}, {"minimum", 0}, {"maximum", 2}}},
                    {"aoPower", {{"type", "number"}, {"minimum", 0.1}, {"maximum", 8}}},
                    {"aoFadeStart",
                     {{"type", "number"}, {"minimum", 0}, {"maximum", 100000}}},
                    {"aoFadeEnd",
                     {{"type", "number"},
                      {"minimum", 0},
                      {"maximum", 100000},
                      {"description", "淡出结束距离，必须大于 aoFadeStart。"}}},
                    {"directionalShadows", {{"type", "boolean"}}},
                    {"shadowResolution",
                     {{"type", "integer"}, {"minimum", 128}, {"maximum", 8192}}},
                    {"shadowBias", {{"type", "number"}, {"minimum", 0}}},
                    {"shadowCascades", {{"type", "integer"}, {"enum", {1, 2, 4}}}},
                    {"shadowDistance",
                     {{"type", "number"},
                      {"minimum", 0},
                      {"maximum", 100000},
                      {"description", "必须大于 0；同时受相机远裁面限制。"}}},
                    {"shadowSplitLambda", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                    {"shadowCascadeBlend", {{"type", "number"}, {"minimum", 0}, {"maximum", 0.3}}},
                    {"clearColor", color(4)}})) &&
           add("set_render_stats", "开启或关闭 bgfx 统计面板，适用于待机、场景预览及玩法。",
               object({{"enabled", {{"type", "boolean"}}}}, {"enabled"})) &&
           add("capture_frame", "请求引擎真实渲染帧 PNG；path 相对于项目 cache，完成需查询。",
               object({{"path", {{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}}}},
                      {"path"})) &&
           add("get_frame_capture", "查询截图实际状态；complete 才表示文件写入完成。",
               object(Json::object()));
}
} // namespace engine::plugins::mcp
