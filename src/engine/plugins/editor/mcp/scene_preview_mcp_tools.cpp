#include "plugins/editor/mcp/scene_preview_mcp_tools.hpp"
#include "plugins/editor/mcp/editor_tool_registration.hpp"

#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#include "plugins/editor/mcp/mcp_system.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace engine::plugins::mcp
{

bool register_scene_preview_tools(McpSystem& system, EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    const Json vector = {{"type", "array"},
                         {"items", {{"type", "number"}}},
                         {"minItems", 3},
                         {"maxItems", 3}};
    const Json camera =
        object_schema({{"position", vector},
                       {"rotation", vector},
                       {"speed", {{"type", "number"}, {"minimum", 0.01}, {"maximum", 1000}}}},
                      Json::array());
    const auto add = [&](std::string name, std::string description, Json schema)
    {
        const auto command = name;
        return register_editor_command_tool(system, editor, std::move(name), std::move(description),
                                            std::move(schema), command);
    };
    return add("open_scene_preview",
               "在编辑器窗口预览场景资源，不启动玩法；替换当前场景并丢弃其未保存内存编辑。"
               "可指定独立镜头位置、角度和移动速度。",
               object_schema({{"path", {{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}}},
                              {"camera", camera}},
                             {"path"})) &&
           add("close_scene_preview", "关闭场景预览，丢弃未保存内存编辑并释放资源，恢复空白窗口。",
               object_schema(Json::object(), Json::array())) &&
           add("get_scene_preview", "查询预览、独立镜头、未保存编辑及等待刷新的状态。",
               object_schema(Json::object(), Json::array())) &&
           add("set_preview_camera",
               "修改独立预览镜头，不写入场景；至少一项 position/rotation/speed。"
               "角度为度，速度为 0.01..1000 米/秒。",
               camera) &&
           add("refresh_scene_preview",
               "增量导入源资源后刷新预览；discardChanges=true 才允许丢弃未保存的编辑。",
               object_schema({{"discardChanges", {{"type", "boolean"}}}}, Json::array()));
}
} // namespace engine::plugins::mcp
