#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <string>

namespace engine::plugins::mcp
{

enum class McpCallStatus
{
    success,
    failed,
    unknown_tool,
    invalid_arguments
};

struct McpTool
{
    std::string name;
    std::string description;
    nlohmann::json input_schema;
    // 输出业务 JSON，由协议层统一包装 MCP 结果；false 表示业务失败。
    std::function<bool(const nlohmann::json&, nlohmann::json&)> execute;
};

} // namespace engine::plugins::mcp
