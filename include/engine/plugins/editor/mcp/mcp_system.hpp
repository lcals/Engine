#pragma once

#include "plugins/editor/mcp/mcp_tool.hpp"
#include "runtime/system/system.hpp"

#include <string_view>
#include <vector>

namespace engine
{

class McpSystem final : public System
{
public:
    McpSystem() noexcept = default;
    ~McpSystem() noexcept override;

    // 初始化后登记表冻结。回调捕获对象由宿主保证存活到本系统关闭。
    [[nodiscard]] bool register_tool(McpTool tool);
    [[nodiscard]] nlohmann::json list_tools() const;
    [[nodiscard]] McpCallStatus call_tool(std::string_view name, const nlohmann::json& arguments,
                                          nlohmann::json& output) const;
    [[nodiscard]] bool run_stdio();

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    std::vector<McpTool> tools_;
};

} // namespace engine
