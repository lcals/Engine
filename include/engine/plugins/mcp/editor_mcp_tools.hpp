#pragma once

#include "plugins/mcp/mcp_target.hpp"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <string_view>

namespace engine
{

class McpSystem;

class EditorMcpTools final
{
public:
    EditorMcpTools(std::filesystem::path project_root, McpTarget target);
    // 登记的回调借用本对象，本对象须存活到 McpSystem 关闭。
    [[nodiscard]] bool register_tools(McpSystem& system);
    // register_tools 成功后供其他模块的工具回调复用；返回执行回执或待确认身份。
    [[nodiscard]] bool call_command(std::string_view command, const nlohmann::json& arguments,
                                    nlohmann::json& reply);

private:
    [[nodiscard]] bool read_receipt(const nlohmann::json& arguments, nlohmann::json& reply);

    std::filesystem::path project_root_;
    McpTarget target_;
};

} // namespace engine
