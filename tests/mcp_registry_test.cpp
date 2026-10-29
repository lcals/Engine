#include "plugins/mcp/mcp_system.hpp"
#include "runtime/engine_config.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace
{
using Json = nlohmann::json;
using engine::McpCallStatus;

bool check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
    return condition;
}

bool exercise()
{
    engine::McpSystem mcp;
    Json result = {{"stale", true}};
    if (!check(mcp.call_tool("fake.sum", Json::object(), result) == McpCallStatus::failed &&
                   result.contains("error") && !result.contains("stale"),
               "uninitialized invocation reports failure with safe output"))
    {
        return false;
    }

    const Json schema = {{"type", "object"},
                         {"properties",
                          {{"values",
                            {{"type", "array"},
                             {"minItems", 1},
                             {"maxItems", 3},
                             {"items", {{"type", "integer"}, {"minimum", -10}, {"maximum", 10}}}}},
                           {"label", {{"type", "string"}, {"minLength", 1}, {"maxLength", 2}}},
                           {"mode", {{"type", "string"}, {"enum", Json::array({"add", "ignore"})}}},
                           {"custom", Json::object()}}},
                         {"required", Json::array({"values"})},
                         {"additionalProperties", false}};
    auto calls = std::make_shared<int>(0);
    std::weak_ptr<int> callback_lifetime = calls;
    engine::McpTool sum{"fake.sum", "Sum bounded input values.", schema,
                        [calls](const Json& input, Json& output)
                        {
                            ++*calls;
                            int total = 0;
                            for (const auto& value : input["values"])
                            {
                                total += value.get<int>();
                            }
                            output = {{"total", total}};
                            return true;
                        }};
    if (!check(mcp.register_tool(std::move(sum)), "module registers its own tool") ||
        !check(!mcp.register_tool(
                   {"fake.sum", "Duplicate", schema, [](const Json&, Json&) { return true; }}),
               "duplicate name rejected") ||
        !check(!mcp.register_tool({"fake.empty", "Missing handler", schema, {}}),
               "empty handler rejected"))
    {
        return false;
    }
    // std::function 移动后源对象可以仍持有小对象回调；本测试只观察注册表的所有权。
    sum.execute = {};
    auto unsupported = schema;
    unsupported["oneOf"] = Json::array();
    if (!check(!mcp.register_tool({"fake.unsupported", "Unsupported schema", unsupported,
                                   [](const Json&, Json&) { return true; }}),
               "unsupported schema cannot silently bypass validation") ||
        !check(mcp.register_tool({"fake.failure",
                                  "Report a business failure.",
                                  {{"type", "object"},
                                   {"properties", Json::object()},
                                   {"additionalProperties", false}},
                                  [](const Json&, Json& output)
                                  {
                                      output = {{"error", "expected failure"}};
                                      return false;
                                  }}),
               "second module registers independently"))
    {
        return false;
    }
    const auto listed = mcp.list_tools();
    if (!check(listed["tools"].size() == 2 && listed["tools"][0]["name"] == "fake.sum" &&
                   listed["tools"][0]["inputSchema"] == schema,
               "list and invocation share the registered description") ||
        !check(mcp.initialize({}), "registry needs no project or assets"))
    {
        return false;
    }
    for (const Json& invalid :
         {Json(false), Json::object(), Json{{"values", Json::array()}},
          Json{{"values", Json::array({1, 2, 3, 4})}}, Json{{"values", Json::array({1.5})}},
          Json{{"values", Json::array({11})}},
          Json{{"values", Json::array({1})}, {"unknown", true}},
          Json{{"values", Json::array({1})}, {"label", "三个字"}},
          Json{{"values", Json::array({1})}, {"mode", "invalid"}}})
    {
        if (!check(mcp.call_tool("fake.sum", invalid, result) == McpCallStatus::invalid_arguments &&
                       result.empty() && *calls == 0,
                   "invalid arguments never execute the handler"))
        {
            return false;
        }
    }
    if (!check(mcp.call_tool("fake.missing", Json::object(), result) ==
                       McpCallStatus::unknown_tool &&
                   result.empty(),
               "unknown tool has a distinct status") ||
        !check(mcp.call_tool("fake.sum",
                             {{"values", Json::array({2, 3})},
                              {"label", "节点"},
                              {"custom", Json::array({1.0, 2.0, 3.0})}},
                             result) == McpCallStatus::success &&
                   result == Json{{"total", 5}} && *calls == 1,
               "valid arguments reach the module callback") ||
        !check(mcp.call_tool("fake.failure", Json::object(), result) == McpCallStatus::failed &&
                   result == Json{{"error", "expected failure"}},
               "business failure keeps its diagnostic output") ||
        !check(!mcp.register_tool({"fake.late", "Late registration", schema,
                                   [](const Json&, Json&) { return true; }}),
               "initialized tool list is frozen"))
    {
        return false;
    }

    std::istringstream input(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":"
        "\"2025-06-18\"}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/"
        "call\",\"params\":{\"name\":\"fake.failure\"}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/"
        "call\",\"params\":{\"name\":\"fake.sum\",\"arguments\":{\"values\":[4]}}}\n");
    std::ostringstream output;
    auto* previous_input = std::cin.rdbuf(input.rdbuf());
    auto* previous_output = std::cout.rdbuf(output.rdbuf());
    const bool completed = mcp.run_stdio();
    std::cin.rdbuf(previous_input);
    std::cout.rdbuf(previous_output);
    std::cin.clear();
    std::istringstream responses(output.str());
    std::string line;
    std::getline(responses, line);
    const auto initialized = Json::parse(line, nullptr, false);
    std::getline(responses, line);
    const auto failed = Json::parse(line, nullptr, false);
    std::getline(responses, line);
    const auto succeeded = Json::parse(line, nullptr, false);
    if (!check(completed && initialized.contains("result") && failed["result"]["isError"] == true &&
                   succeeded["result"]["isError"] == false &&
                   Json::parse(succeeded["result"]["content"][0]["text"].get<std::string>(),
                               nullptr, false) == Json{{"total", 4}},
               "stdio wraps generic module results and business errors"))
    {
        return false;
    }
    calls.reset();
    if (!check(!callback_lifetime.expired(), "registry owns its callbacks"))
    {
        return false;
    }
    mcp.shutdown();
    return check(mcp.list_tools()["tools"].empty() && callback_lifetime.expired(),
                 "shutdown releases registered callbacks");
}
} // namespace

int main()
{
    return exercise() ? 0 : 1;
}
