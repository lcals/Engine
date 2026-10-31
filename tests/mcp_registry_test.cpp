#include "plugins/editor/mcp/mcp_system.hpp"
#include "runtime/engine_config.hpp"

#if defined(ENGINE_TEST_SCENE_BAKE_TOOLS) || defined(ENGINE_TEST_RENDER_TOOLS)
#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#endif

#if defined(ENGINE_TEST_RENDER_TOOLS)
#include "plugins/editor/mcp/render_mcp_tools.hpp"
#endif

#if defined(ENGINE_TEST_SCENE_BAKE_TOOLS)
#include "plugins/editor/scene_bake/scene_bake_mcp_tools.hpp"
#endif

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
using engine::plugins::mcp::McpCallStatus;

bool check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
    return condition;
}

bool exercise_bake_tools()
{
#if defined(ENGINE_TEST_SCENE_BAKE_TOOLS)
    engine::plugins::mcp::EditorMcpTools editor({}, {});
    engine::plugins::scene_bake::SceneBakeMcpTools tools(editor);
    engine::plugins::mcp::McpSystem mcp;
    if (!check(tools.register_tools(mcp) && mcp.initialize({}),
               "scene bake tools provide valid registration schemas") ||
        !check(mcp.list_tools()["tools"].size() == 3,
               "scene bake registers start, query and cancel tools"))
    {
        return false;
    }
    Json result;
    return check(mcp.call_tool("bake_scene",
                               {{"scene", "project/scenes/test.scene"},
                                {"environment", "project/sky.png"},
                                {"effects", Json::array({"unknown"})}},
                               result) == McpCallStatus::invalid_arguments &&
                     result.empty(),
                 "unknown bake effects are rejected before editor transport");
#else
    return true;
#endif
}

bool exercise_render_tools()
{
#if defined(ENGINE_TEST_RENDER_TOOLS)
    engine::plugins::mcp::EditorMcpTools editor({}, {});
    engine::plugins::mcp::RenderMcpTools tools(editor);
    engine::plugins::mcp::McpSystem mcp;
    if (!check(tools.register_tools(mcp) && mcp.initialize({}),
               "render tools register schemas supported by the MCP protocol") ||
        !check(mcp.list_tools()["tools"].size() == 5, "all render tools are discoverable"))
    {
        return false;
    }
    Json schema;
    const auto listed = mcp.list_tools();
    for (const auto& tool : listed["tools"])
    {
        if (tool["name"] == "set_render_settings")
        {
            schema = tool["inputSchema"];
        }
    }
    engine::plugins::mcp::McpSystem probe;
    if (!check(schema.is_object() &&
                   probe.register_tool({"render.settings.schema", "Validate render inputs", schema,
                                         [](const Json& input, Json& output)
                                         {
                                             output = input;
                                             return true;
                                         }}) &&
                   probe.initialize({}),
               "render settings schema is reusable without editor transport"))
    {
        return false;
    }
    Json result;
    for (const auto count : {1, 2, 4})
    {
        const Json input = {{"shadowCascades", count},
                            {"shadowDistance", 150},
                            {"shadowSplitLambda", .6},
                            {"shadowCascadeBlend", .1},
                            {"aoEnabled", true},
                            {"aoRadius", 1.4},
                            {"aoStrength", .8},
                            {"aoPower", 2},
                            {"aoFadeStart", 40},
                            {"aoFadeEnd", 140},
                            {"hdrEnabled", true},
                            {"bloomEnabled", true},
                            {"bloomThreshold", 1.2},
                            {"bloomIntensity", .3},
                            {"bloomRadius", 2}};
        if (!check(probe.call_tool("render.settings.schema", input, result) ==
                           McpCallStatus::success &&
                       result == input,
                   "valid render effect values pass the published tool schema"))
        {
            return false;
        }
    }
    for (const Json& invalid :
         {Json{{"shadowCascades", 3}}, Json{{"shadowCascades", 2.0}},
          Json{{"shadowDistance", -1}}, Json{{"shadowDistance", 100001}},
          Json{{"shadowSplitLambda", 1.1}}, Json{{"shadowCascadeBlend", .31}},
          Json{{"aoEnabled", 1}}, Json{{"aoRadius", -1}}, Json{{"aoRadius", 101}},
          Json{{"aoStrength", 2.1}}, Json{{"aoPower", .09}}, Json{{"aoPower", 8.1}},
          Json{{"aoFadeStart", -1}}, Json{{"aoFadeEnd", 100001}},
          Json{{"hdrEnabled", 1}}, Json{{"bloomEnabled", 1}},
          Json{{"bloomThreshold", -1}}, Json{{"bloomThreshold", 101}},
          Json{{"bloomIntensity", 10.1}}, Json{{"bloomRadius", .24}},
          Json{{"bloomRadius", 4.1}}})
    {
        if (!check(mcp.call_tool("set_render_settings", invalid, result) ==
                           McpCallStatus::invalid_arguments &&
                       result.empty(),
                   "invalid render effect values are rejected before editor transport"))
        {
            return false;
        }
    }
#endif
    return true;
}

bool exercise()
{
    engine::plugins::mcp::McpSystem mcp;
    Json result = {{"stale", true}};
    if (!check(mcp.call_tool("fake.sum", Json::object(), result) ==
                       engine::plugins::mcp::McpCallStatus::failed &&
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
    engine::plugins::mcp::McpTool sum{"fake.sum", "Sum bounded input values.", schema,
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
        if (!check(mcp.call_tool("fake.sum", invalid, result) ==
                           engine::plugins::mcp::McpCallStatus::invalid_arguments &&
                       result.empty() && *calls == 0,
                   "invalid arguments never execute the handler"))
        {
            return false;
        }
    }
    if (!check(mcp.call_tool("fake.missing", Json::object(), result) ==
                       engine::plugins::mcp::McpCallStatus::unknown_tool &&
                   result.empty(),
               "unknown tool has a distinct status") ||
        !check(mcp.call_tool("fake.sum",
                             {{"values", Json::array({2, 3})},
                              {"label", "节点"},
                              {"custom", Json::array({1.0, 2.0, 3.0})}},
                             result) == engine::plugins::mcp::McpCallStatus::success &&
                   result == Json{{"total", 5}} && *calls == 1,
               "valid arguments reach the module callback") ||
        !check(mcp.call_tool("fake.failure", Json::object(), result) ==
                       engine::plugins::mcp::McpCallStatus::failed &&
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
    return exercise() && exercise_bake_tools() && exercise_render_tools() ? 0 : 1;
}
