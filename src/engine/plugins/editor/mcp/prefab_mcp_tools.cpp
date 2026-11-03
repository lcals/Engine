#include "plugins/editor/mcp/component_mcp_tools.hpp"

namespace engine::plugins::mcp
{

bool register_prefab_tools(McpSystem& system, EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    const Json path = {{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}};
    const Json identity = {{"type", "string"}, {"minLength", 1}};
    const auto add = [&system, &editor](std::string name, std::string description, Json properties,
                                        Json required)
    {
        const auto command = name;
        return system.register_tool({std::move(name),
                                     std::move(description),
                                     {{"type", "object"},
                                      {"properties", std::move(properties)},
                                      {"required", std::move(required)},
                                      {"additionalProperties", false}},
                                     [&editor, command](const Json& arguments, Json& reply)
                                     { return editor.call_command(command, arguments, reply); }});
    };
    const Json fields = {{"type", "array"},
                         {"items", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                         {"minItems", 1}};
    const Json override_properties = {
        {"fields", fields},
        {"componentType", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}};
    return add("instantiate_prefab", "实例化独立预制体节点树并保留来源；支持撤销。",
               {{"sceneId", identity},
                {"path", path},
                {"name", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}},
               {"sceneId", "path", "name"}) &&
           add("create_prefab_from_model",
               "从已导入 Model 创建关联预制体；model 为资源路径，path 为新的 .prefab 路径。",
               {{"model", path}, {"path", path}}, {"model", "path"}) &&
           tools::component_tool(
               system, editor, "create_prefab",
               "将选中节点子树保存为独立预制体；不替换原节点，不把场景标记为已保存。",
               {{"path", path}}, {"path"}) &&
           tools::component_tool(system, editor, "get_prefab_instance",
                                 "查询节点所属预制体、源节点身份和显式覆盖字段。",
                                 Json::object()) &&
           tools::component_tool(system, editor, "set_prefab_override",
                                 "把当前字段值明确登记为实例覆盖，包括与模板相同的值；componentType"
                                 " 省略时编辑节点状态。",
                                 override_properties, {"fields"}) &&
           tools::component_tool(
               system, editor, "revert_prefab_override",
               "清除指定覆盖并恢复最新模板字段值；componentType 使用组件生成类型名。支持撤销。",
               override_properties, {"fields"}) &&
           tools::component_tool(
               system, editor, "link_prefab",
               "将原样导出模板的源子树关联到该模板；要求保留稳定节点身份。支持撤销。",
               {{"path", path}}, {"path"}) &&
           tools::component_tool(
               system, editor, "unlink_prefab",
               "解除实例根的模板关联并保留现有节点；后续修改不再跟随模板。支持撤销。",
               Json::object());
}

} // namespace engine::plugins::mcp
