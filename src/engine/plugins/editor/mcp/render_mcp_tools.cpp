#include "plugins/editor/mcp/render_mcp_tools.hpp"
#include "plugins/editor/mcp/editor_tool_registration.hpp"

#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#include "plugins/editor/mcp/mcp_system.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace engine::plugins::mcp
{

bool register_render_tools(McpSystem& system, EditorMcpTools& editor)
{
    using Json = nlohmann::json;
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
        return register_editor_command_tool(system, editor, std::move(name), std::move(description),
                                            std::move(schema), command);
    };
    const Json identity = {{"sceneId", {{"type", "string"}, {"minLength", 1}, {"maxLength", 20}}},
                           {"nodeId", {{"type", "string"}, {"minLength", 1}, {"maxLength", 20}}}};
    const Json required_node = {"sceneId", "nodeId"};
    auto planar = identity;
    planar["enabled"] = {{"type", "boolean"}};
    planar["resolutionScale"] = {{"type", "number"}, {"minimum", .25}, {"maximum", 1}};
    planar["clipBias"] = {{"type", "number"}, {"minimum", 0}, {"maximum", 1}};
    auto probes = identity;
    probes["enabled"] = {{"type", "boolean"}};
    probes["resource"] = {{"type", "string"}, {"maxLength", 1024}};
    auto mesh = identity;
    mesh["castShadows"] = {{"type", "boolean"}};
    mesh["bakeStatic"] = {{"type", "boolean"}};
    mesh["lightmap"] = {{"type", "string"}, {"maxLength", 1024}};
    auto selected = identity;
    selected["slot"] = {{"type", "integer"}, {"minimum", 0}, {"maximum", 65535}};
    auto feature = selected;
    feature["feature"] = {
        {"type", "string"},
        {"enum",
         {"frustum_culling", "static_instancing", "static_lod", "wireframe", "debug_draw",
          "mesh_bounds", "camera_frusta", "stats", "atmosphere", "hex_tiling"}}};
    feature["enabled"] = {{"type", "boolean"}};
    const auto vector =
        Json{{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}};
    const auto line =
        object_schema({{"start", vector},
                       {"end", vector},
                       {"color", {{"type", "integer"}, {"minimum", 0}, {"maximum", 4294967295ULL}}},
                       {"depthTest", {{"type", "boolean"}}}},
                      {"start", "end"});
    const bool feature_set = system.register_tool(
        {"set_render_feature",
         "统一编辑对应组件运行属性或MCP临时诊断；组件配置可独立于当前设备支持保存，不代表GPU已生效"
         "。",
         object_schema(std::move(feature), {"feature", "enabled"}),
         [&editor](const Json& arguments, Json& reply)
         {
             const auto& name = arguments["feature"].get_ref<const std::string&>();
             const bool enabled = arguments["enabled"].get<bool>();
             Json forwarded;
             std::string command;
             if (name == "frustum_culling" || name == "static_instancing" || name == "static_lod")
             {
                 if (!arguments.contains("sceneId") || !arguments.contains("nodeId") ||
                     arguments.contains("slot"))
                 {
                     reply = {
                         {"error", "Runtime features require sceneId/nodeId component identity"}};
                     return false;
                 }
                 forwarded = {{"sceneId", arguments["sceneId"]}, {"nodeId", arguments["nodeId"]}};
                 if (name == "frustum_culling")
                 {
                     command = "set_camera";
                     forwarded["frustumCullingEnabled"] = enabled;
                 }
                 else
                 {
                     command = "set_mesh_render_settings";
                     forwarded[name == "static_instancing" ? "staticInstancingEnabled"
                                                           : "staticLodEnabled"] = enabled;
                 }
             }
             else if (name == "hex_tiling")
             {
                 if (!arguments.contains("sceneId") || !arguments.contains("nodeId") ||
                     !arguments.contains("slot"))
                 {
                     reply = {{"error", "hex_tiling requires sceneId, nodeId and slot"}};
                     return false;
                 }
                 command = "set_material_parameter";
                 forwarded = {{"sceneId", arguments["sceneId"]},
                              {"nodeId", arguments["nodeId"]},
                              {"slot", arguments["slot"]},
                              {"name", "hexTilingEnabled"},
                              {"type", "float"},
                              {"value", enabled ? 1 : 0}};
             }
             else if (name == "atmosphere")
             {
                 if (!arguments.contains("sceneId") || !arguments.contains("nodeId") ||
                     arguments.contains("slot"))
                 {
                     reply = {{"error", "atmosphere requires sceneId/nodeId environment identity"}};
                     return false;
                 }
                 command = "set_render_settings";
                 forwarded = {{"sceneId", arguments["sceneId"]},
                              {"nodeId", arguments["nodeId"]},
                              {"atmosphereEnabled", enabled}};
             }
             else
             {
                 if (arguments.size() != 2)
                 {
                     reply = {{"error", "Renderer features do not accept scene/material identity"}};
                     return false;
                 }
                 command = "set_render_debug_feature";
                 forwarded = {{"feature", name}, {"enabled", enabled}};
             }
             return editor.call_command(command, forwarded, reply);
         }});
    auto lod = Json{{"path", {{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}}}};
    auto lod_set = lod;
    lod_set["enabled"] = {{"type", "boolean"}};
    lod_set["triangleRatios"] = {{"type", "array"},
                                 {"minItems", 1},
                                 {"items", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}}};
    lod_set["screenHeights"] = lod_set["triangleRatios"];
    lod_set["maxRelativeError"] = {{"type", "number"}, {"minimum", 0}, {"maximum", 1}};
    return feature_set &&
           add("get_render_features", "统一查询设备、当前场景和指定材质的实际功能状态与支持原因。",
               object_schema(std::move(selected), Json::array())) &&
           add("get_mesh_render_settings", "读取指定MeshRenderer的实例化和LOD组件属性。",
               object_schema(identity, required_node)) &&
           add("set_mesh_render_settings",
               "编辑指定MeshRenderer的实例化和LOD属性，支持撤销和场景保存。",
               object_schema({{"sceneId", identity["sceneId"]},
                              {"nodeId", identity["nodeId"]},
                              {"staticInstancingEnabled", {{"type", "boolean"}}},
                              {"staticLodEnabled", {{"type", "boolean"}}}},
                             required_node)) &&
           add("get_render_submission_stats", "按实际view返回本模块mesh提交计数，不代表GPU完成。",
               object_schema(Json::object(), Json::array())) &&
           add("draw_debug_lines", "提交临时3D调试线，使用真实观察镜头；必须先开启debug_draw。",
               object_schema(
                   {{"lines",
                     {{"type", "array"}, {"items", line}, {"minItems", 1}, {"maxItems", 4096}}},
                    {"lifetimeFrames", {{"type", "integer"}, {"minimum", 1}, {"maximum", 600}}}},
                   {"lines"})) &&
           add("get_mesh_lod_import", "读取源资源LOD导入参数，不修改资源。",
               object_schema(std::move(lod), {"path"})) &&
           add("set_mesh_lod_import", "原子保存源资源LOD参数并通过正式管线重新导入，保留GUID。",
               object_schema(std::move(lod_set), {"path"})) &&
           add("get_render_settings", "读取当前环境光、雾、曝光、阴影与背景设置。",
               object_schema(Json::object(), Json::array())) &&
           add("set_render_settings",
               "编辑场景环境、相机后处理和方向光阴影，支持保存及撤销；无场景时只接受 clearColor。",
               object_schema(
                   {{"sceneId", identity["sceneId"]},
                    {"nodeId", identity["nodeId"]},
                    {"atmosphereEnabled", {{"type", "boolean"}}},
                    {"atmosphereSunNodeId",
                     {{"type", "string"}, {"minLength", 1}, {"maxLength", 20}}},
                    {"atmosphereGroundHeight", {{"type", "number"}}},
                    {"atmosphereRayleighScattering", color(3)},
                    {"atmosphereRayleighScaleHeight", {{"type", "number"}, {"minimum", 0}}},
                    {"atmosphereMieScattering", {{"type", "number"}, {"minimum", 0}}},
                    {"atmosphereMieAbsorption", {{"type", "number"}, {"minimum", 0}}},
                    {"atmosphereMieScaleHeight", {{"type", "number"}, {"minimum", 0}}},
                    {"atmosphereMieAnisotropy",
                     {{"type", "number"}, {"minimum", -0.999}, {"maximum", 0.999}}},
                    {"atmosphereOzoneAbsorption", color(3)},
                    {"atmosphereOzoneCenterHeight", {{"type", "number"}, {"minimum", 0}}},
                    {"atmosphereOzoneHalfWidth", {{"type", "number"}, {"minimum", 0}}},
                    {"atmospherePlanetRadius", {{"type", "number"}, {"minimum", 0}}},
                    {"atmosphereHeight", {{"type", "number"}, {"minimum", 0}}},
                    {"atmosphereGroundAlbedo", color(3)},
                    {"atmosphereMultiscatterStrength", {{"type", "number"}, {"minimum", 0}}},
                    {"ambientColor", color(3)},
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
                    {"bloomThreshold", {{"type", "number"}, {"minimum", 0}, {"maximum", 100}}},
                    {"bloomIntensity", {{"type", "number"}, {"minimum", 0}, {"maximum", 10}}},
                    {"bloomRadius", {{"type", "number"}, {"minimum", 0.25}, {"maximum", 4}}},
                    {"ssrEnabled", {{"type", "boolean"}}},
                    {"ssrResolutionScale", {{"type", "number"}, {"minimum", .25}, {"maximum", 1}}},
                    {"ssrMaxSteps", {{"type", "integer"}, {"minimum", 8}, {"maximum", 256}}},
                    {"ssrMaxDistance",
                     {{"type", "number"},
                      {"minimum", 0},
                      {"maximum", 100000},
                      {"description", "必须大于 0。"}}},
                    {"ssrThickness",
                     {{"type", "number"},
                      {"minimum", 0},
                      {"maximum", 10},
                      {"description", "必须大于 0。"}}},
                    {"ssrRoughnessCutoff", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                    {"aoRadius",
                     {{"type", "number"},
                      {"minimum", 0},
                      {"maximum", 100},
                      {"description", "世界空间半径，必须大于 0。"}}},
                    {"aoStrength", {{"type", "number"}, {"minimum", 0}, {"maximum", 2}}},
                    {"aoPower", {{"type", "number"}, {"minimum", 0.1}, {"maximum", 8}}},
                    {"aoFadeStart", {{"type", "number"}, {"minimum", 0}, {"maximum", 100000}}},
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
                    {"clearColor", color(4)}},
                   Json::array())) &&
           add("get_planar_reflection", "读取已有平面反射组件。",
               object_schema(identity, required_node)) &&
           add("set_planar_reflection", "编辑平面反射；可为已有 MeshRenderer 添加组件，支持撤销。",
               object_schema(std::move(planar), required_node)) &&
           add("get_light_probe_volume", "读取探针配置资源与当前变换下的有效性。",
               object_schema(identity, required_node)) &&
           add("set_light_probe_volume", "编辑探针体积，resource 使用逻辑路径，空字符串清空绑定。",
               object_schema(std::move(probes), required_node)) &&
           add("get_mesh_lighting", "读取已有 MeshRenderer 的阴影、烘焙标记及 Lightmap 有效性。",
               object_schema(identity, required_node)) &&
           add("set_mesh_lighting", "编辑已有网格的照明字段；不会生成 UV2 或执行烘焙。",
               object_schema(std::move(mesh), required_node)) &&
           add("set_render_stats", "开启或关闭 bgfx 统计面板，适用于待机、场景预览及玩法。",
               object_schema({{"enabled", {{"type", "boolean"}}}}, {"enabled"})) &&
           add("capture_frame", "请求引擎真实渲染帧 PNG；path 相对于项目 cache，完成需查询。",
               object_schema(
                   {{"path", {{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}}}},
                   {"path"})) &&
           add("get_frame_capture", "查询截图实际状态；complete 才表示文件写入完成。",
               object_schema(Json::object(), Json::array()));
}
} // namespace engine::plugins::mcp
