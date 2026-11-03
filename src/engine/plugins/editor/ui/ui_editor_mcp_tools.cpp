#include "plugins/editor/ui/ui_editor_mcp_tools.hpp"

#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#include "plugins/editor/mcp/mcp_system.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace engine::plugins::ui
{
bool register_ui_editor_tools(mcp::McpSystem& system, mcp::EditorMcpTools& editor)
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
        auto command = name;
        return system.register_tool({std::move(name), std::move(description), std::move(schema),
                                     [&editor, command](const Json& args, Json& reply)
                                     { return editor.call_command(command, args, reply); }});
    };
    const Json id{{"type", "string"}, {"minLength", 1}, {"maxLength", 20}};
    const Json short_text{{"type", "string"}, {"minLength", 1}, {"maxLength", 128}};
    const Json path{{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}};
    const Json target =
        object({{"documentId", id}, {"elementId", id}}, {"documentId", "elementId"});
    const Json point{{"type", "number"}, {"minimum", 0}};
    const Json boolean{{"type", "boolean"}};
    const Json button{{"type", "string"}, {"enum", {"left", "middle", "right"}}};
    const Json clicks{{"type", "integer"}, {"minimum", 1}, {"maximum", 2}};
    const Json event =
        object({{"type",
                 {{"type", "string"},
                  {"enum",
                   {"pointer_move", "pointer_down", "pointer_up", "wheel", "focus", "key_down",
                    "key_up", "text", "wait"}}}},
                {"target", target},
                {"x", point},
                {"y", point},
                {"button", button},
                {"clickCount", clicks},
                {"deltaX", {{"type", "number"}, {"minimum", -1000}, {"maximum", 1000}}},
                {"deltaY", {{"type", "number"}, {"minimum", -1000}, {"maximum", 1000}}},
                {"key", short_text},
                {"repeat", boolean},
                {"text", {{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}}},
                {"milliseconds", {{"type", "number"}, {"minimum", 0}, {"maximum", 10000}}}},
               {"type"});
    const Json fonts{{"type", "array"},
                     {"minItems", 1},
                     {"maxItems", 64},
                     {"items", object({{"family", short_text}, {"asset", path}, {"shader", path}},
                                      {"family", "asset"})}};
    const Json textures{{"type", "array"}, {"maxItems", 256}, {"items", path}};
    const Json hash{{"type", "string"}, {"maxLength", 128}};
    return add("get_ui_documents",
               "查询所有已发布场景（包含常驻节点）的 UI 绑定、真实文档身份及逻辑/像素窗口尺寸。",
               object(Json::object())) &&
           add("set_ui_document",
               "在可编辑预览中添加或修改 UiComponent 文档绑定；支持撤销和场景保存。",
               object({{"sceneId", id}, {"nodeId", id}, {"document", path}, {"enabled", boolean}},
                      {"sceneId", "nodeId", "document"})) &&
           add("get_ui_tree",
               "按实际文档代次读取 RmlUi 树、布局和控件身份；分页不保证跨 View 变更的同一快照。",
               object({{"nodeId", id},
                       {"documentId", id},
                       {"root", target},
                       {"offset", {{"type", "integer"}, {"minimum", 0}, {"maximum", 8192}}},
                       {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1024}}},
                       {"depth", {{"type", "integer"}, {"minimum", 0}, {"maximum", 64}}}},
                      {"nodeId", "documentId"})) &&
           add("get_ui_element",
               "读取完整控件句柄的文字、有效可见/启用、焦点、逻辑矩形及计算样式；密码值返回掩码。",
               object({{"target", target},
                       {"styles", {{"type", "array"}, {"maxItems", 64}, {"items", short_text}}}},
                      {"target"})) &&
           add("set_ui_element",
               "仅修改当前 View。text/value/visible/enabled/image 使用 value；class 使用 "
               "name+active；style 使用 name+value。不改变 TS Model，不保存源文档。",
               object({{"target", target},
                       {"operation",
                        {{"type", "string"},
                         {"enum",
                          {"text", "value", "visible", "enabled", "class", "style", "image"}}}},
                       {"value", Json::object()},
                       {"name", short_text},
                       {"active", boolean}},
                      {"target", "operation"})) &&
           add("get_ui_source",
               "path 提交异步原始 RML/meta 读取；job "
               "查询读或保存结果。不存在的新路径返回空哈希，不能从运行 DOM 保存源。",
               object({{"path", path}, {"job", id}})) &&
           add("save_ui_source",
               "异步校验并原子保存项目内 RML 与相邻 meta，保留 "
               "GUID。必须传入读取得到的两个哈希；返回 job 用 get_ui_source "
               "查询。source_saved/import:pending 尚未表示 View 已刷新。",
               object({{"path", path},
                       {"source", {{"type", "string"}, {"maxLength", 1048576}}},
                       {"expectedSourceHash", hash},
                       {"expectedMetaHash", hash},
                       {"fonts", fonts},
                       {"textures", textures}},
                      {"path", "source", "expectedSourceHash", "expectedMetaHash"})) &&
           add("ui_input",
               "真实 UI 输入序列，最多 256 事件/10 秒；down/up "
               "必须配对，逻辑坐标。支持拖动、滚轮、focus、Unicode text、组合键和非阻塞 "
               "wait；真人输入优先取消。键名为 A-Z/0-9 或 "
               "Enter/Tab/Space/Left/Right/Up/Down/Home/End/PageUp/PageDown/Delete/Backspace/"
               "Insert/Escape/Shift/Control/Alt/Meta/RightShift/RightControl/RightAlt/RightMeta/"
               "F1-F12。",
               object({{"documentId", id},
                       {"events",
                        {{"type", "array"}, {"minItems", 1}, {"maxItems", 256}, {"items", event}}}},
                      {"documentId", "events"})) &&
           add("get_ui_input",
               "查询输入作业的状态、已完成事件数、失败位置和最后焦点/命中，不消费 TS 业务事件。",
               object({{"job", id}}, {"job"})) &&
           add("cancel_ui_input", "停止待执行输入并安全释放模拟按压；已经发送的合法输入不会回滚。",
               object({{"job", id}}, {"job"})) &&
           add("click_ui",
               "用同一输入作业组合 "
               "move/down/up；目标中心被遮挡或不可交互时失败，不直接调用业务点击处理器。",
               object({{"target", target},
                       {"x", point},
                       {"y", point},
                       {"button", button},
                       {"clickCount", clicks}},
                      {"target"}));
}
} // namespace engine::plugins::ui
