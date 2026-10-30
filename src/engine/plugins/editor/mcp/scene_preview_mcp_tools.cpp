#include "plugins/editor/mcp/scene_preview_mcp_tools.hpp"

#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#include "plugins/editor/mcp/mcp_system.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace engine::plugins::mcp
{
ScenePreviewMcpTools::ScenePreviewMcpTools(EditorMcpTools& editor) noexcept : editor_(editor)
{
}

bool ScenePreviewMcpTools::register_tools(McpSystem& system)
{
    using Json = nlohmann::json;
    const auto object = [](Json properties, Json required = Json::array())
    {
        return Json{{"type", "object"},
                    {"properties", std::move(properties)},
                    {"required", std::move(required)},
                    {"additionalProperties", false}};
    };
    const Json vector = {{"type", "array"},
                         {"items", {{"type", "number"}}},
                         {"minItems", 3},
                         {"maxItems", 3}};
    const Json camera = object({{"position", vector},
                                {"rotation", vector},
                                {"speed", {{"type", "number"},
                                           {"minimum", 0.01}, {"maximum", 1000}}}});
    const auto add = [&](std::string name, std::string description, Json schema)
    {
        const auto command = name;
        return system.register_tool({std::move(name), std::move(description), std::move(schema),
                                     [this, command](const Json& arguments, Json& reply)
                                     { return editor_.call_command(command, arguments, reply); }});
    };
    return add("open_scene_preview",
               "在编辑器窗口预览场景资源，不启动玩法；替换当前场景并丢弃其未保存内存编辑。"
               "可指定独立镜头位置、角度和移动速度。",
               object({{"path", {{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}}},
                       {"camera", camera}},
                      {"path"})) &&
           add("close_scene_preview", "关闭场景预览，丢弃未保存内存编辑并释放资源，恢复空白窗口。",
               object(Json::object())) &&
           add("get_scene_preview", "查询预览、独立镜头、未保存编辑及等待刷新的状态。",
               object(Json::object())) &&
           add("set_preview_camera",
               "修改独立预览镜头，不写入场景；至少一项 position/rotation/speed。"
               "角度为度，速度为 0.01..1000 米/秒。",
               camera) &&
           add("refresh_scene_preview",
               "增量导入源资源后刷新预览；discardChanges=true 才允许丢弃未保存的编辑。",
               object({{"discardChanges", {{"type", "boolean"}}}}));
}
} // namespace engine::plugins::mcp
