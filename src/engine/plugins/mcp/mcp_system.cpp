#include "plugins/mcp/mcp_system.hpp"

#include "plugins/mcp/mcp_schema.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <utility>

namespace engine
{
namespace
{
using Json = nlohmann::json;

Json rpc_error(const Json& id, int code, std::string_view message)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

Json rpc_result(const Json& id, Json result)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

Json tool_result(Json content, bool failed = false)
{
    return {
        {"content",
         Json::array({{{"type", "text"},
                       {"text", content.dump(-1, ' ', false, Json::error_handler_t::replace)}}})},
        {"isError", failed}};
}

} // namespace

McpSystem::~McpSystem() noexcept
{
    shutdown();
}

bool McpSystem::register_tool(McpTool tool)
{
    if (is_initialized() || tool.name.empty() || tool.description.empty() || !tool.execute ||
        !mcp_protocol::valid_schema(tool.input_schema) ||
        std::any_of(tools_.begin(), tools_.end(),
                    [&tool](const auto& current) { return current.name == tool.name; }))
    {
        return false;
    }
    tools_.push_back(std::move(tool));
    return true;
}

Json McpSystem::list_tools() const
{
    Json result = {{"tools", Json::array()}};
    for (const auto& tool : tools_)
    {
        result["tools"].push_back({{"name", tool.name},
                                   {"description", tool.description},
                                   {"inputSchema", tool.input_schema}});
    }
    return result;
}

McpCallStatus McpSystem::call_tool(std::string_view name, const Json& arguments, Json& output) const
{
    output = Json::object();
    if (!is_initialized())
    {
        output["error"] = "MCP tool registry is not initialized";
        return McpCallStatus::failed;
    }
    const auto tool = std::find_if(tools_.begin(), tools_.end(),
                                   [name](const auto& item) { return item.name == name; });
    if (tool == tools_.end())
    {
        return McpCallStatus::unknown_tool;
    }
    if (!mcp_protocol::valid_arguments(tool->input_schema, arguments))
    {
        return McpCallStatus::invalid_arguments;
    }
    return tool->execute(arguments, output) ? McpCallStatus::success : McpCallStatus::failed;
}

bool McpSystem::on_initialize(const EngineConfig&)
{
    return true;
}

void McpSystem::on_shutdown() noexcept
{
    tools_.clear();
}

bool McpSystem::run_stdio()
{
    if (!is_initialized())
    {
        return false;
    }
    bool initialized = false;
    bool ready = false;
    std::string line;
    while (std::getline(std::cin, line))
    {
        Json reply;
        const auto request = Json::parse(line, nullptr, false);
        if (request.is_discarded())
        {
            reply = rpc_error(nullptr, -32700, "Parse error");
        }
        else if (!request.is_object() || !request.contains("jsonrpc") ||
                 request["jsonrpc"] != "2.0" || !request.contains("method") ||
                 !request["method"].is_string() ||
                 (request.contains("id") && !request["id"].is_null() &&
                  !request["id"].is_string() && !request["id"].is_number_integer()))
        {
            reply = rpc_error(nullptr, -32600, "Invalid Request");
        }
        else
        {
            const auto method = request["method"].get<std::string>();
            if (!request.contains("id"))
            {
                if (method == "notifications/initialized" && initialized)
                {
                    ready = true;
                }
                continue;
            }
            const auto& id = request["id"];
            const auto params = request.contains("params") ? request["params"] : Json::object();
            if (!params.is_object())
            {
                reply = rpc_error(id, -32602, "Invalid params");
            }
            else if (method == "initialize")
            {
                if (initialized || !params.contains("protocolVersion") ||
                    !params["protocolVersion"].is_string())
                {
                    reply =
                        rpc_error(id, -32602, "Invalid initialize params or already initialized");
                }
                else
                {
                    initialized = true;
                    const auto requested = params["protocolVersion"].get<std::string>();
                    const std::string protocol =
                        requested == "2024-11-05" ? requested : "2025-06-18";
                    reply = rpc_result(
                        id,
                        {{"protocolVersion", protocol},
                         {"capabilities", {{"tools", {{"listChanged", false}}}}},
                         {"serverInfo", {{"name", "engine-editor-tools"}, {"version", "0.1.0"}}}});
                }
            }
            else if (method == "ping")
            {
                reply = rpc_result(id, Json::object());
            }
            else if (!ready)
            {
                reply =
                    rpc_error(id, -32000, "Initialize and send notifications/initialized first");
            }
            else if (method == "tools/list")
            {
                reply = rpc_result(id, list_tools());
            }
            else if (method == "tools/call")
            {
                const auto arguments =
                    params.contains("arguments") ? params["arguments"] : Json::object();
                if (!params.contains("name") || !params["name"].is_string() ||
                    !arguments.is_object())
                {
                    reply = rpc_error(id, -32602, "Invalid tool call params");
                }
                else
                {
                    Json result;
                    const auto status =
                        call_tool(params["name"].get_ref<const std::string&>(), arguments, result);
                    if (status == McpCallStatus::unknown_tool ||
                        status == McpCallStatus::invalid_arguments)
                    {
                        reply = rpc_error(id, -32602,
                                          status == McpCallStatus::unknown_tool
                                              ? "Unknown tool"
                                              : "Invalid tool arguments");
                    }
                    else
                    {
                        reply = rpc_result(
                            id, tool_result(std::move(result), status != McpCallStatus::success));
                    }
                }
            }
            else
            {
                reply = rpc_error(id, -32601, "Method not found");
            }
        }
        std::cout << reply.dump(-1, ' ', false, Json::error_handler_t::replace) << '\n'
                  << std::flush;
        if (!std::cout)
        {
            return false;
        }
    }
    return std::cin.eof();
}

} // namespace engine
