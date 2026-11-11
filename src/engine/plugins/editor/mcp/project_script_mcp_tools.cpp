#include "plugins/editor/mcp/project_script_mcp_tools.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/editor_tools.hpp"
#include "editor/asset_pipeline/import_tool.hpp"
#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#include "plugins/editor/mcp/project_tool_utils.hpp"
#include "plugins/runtime/mcp/mcp_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cstdint>
#include <string>
#include <system_error>

namespace engine::plugins::mcp
{
namespace
{
namespace fs = std::filesystem;
namespace pipeline = engine::editor::asset_pipeline;
using Json = nlohmann::json;
constexpr std::size_t script_limit = 256 * 1024;

bool fail(Json& result, std::string error)
{
    result = {{"error", std::move(error)}};
    return false;
}

bool execute(EditorMcpTools& editor, const fs::path& project, const McpTarget& target,
             const Json& arguments, Json& result)
{
    if (!arguments.is_object() || arguments.size() < 4 || arguments.size() > 5 ||
        !arguments.contains("session") || !arguments["session"].is_string() ||
        !arguments.contains("runId") || !arguments["runId"].is_string() ||
        !arguments.contains("sourcePath") || !arguments["sourcePath"].is_string() ||
        !arguments.contains("source") || !arguments["source"].is_string())
    {
        return fail(result, "Project script requires session, runId, sourcePath and source");
    }
    const auto& run_text = arguments["runId"].get_ref<const std::string&>();
    std::uint64_t run = 0;
    const auto parsed = std::from_chars(run_text.data(), run_text.data() + run_text.size(), run);
    const auto& source = arguments["source"].get_ref<const std::string&>();
    const auto values = arguments.value("arguments", Json());
    if (parsed.ec != std::errc{} || parsed.ptr != run_text.data() + run_text.size() || run == 0 ||
        source.empty() || source.size() > script_limit ||
        values.dump(-1, ' ', false, Json::error_handler_t::replace).size() > script_limit)
    {
        return fail(result, "Project script requires a nonzero Run and at most 256 KiB per input");
    }
    Json status;
    if (!tools::read_project_status(target.runtime_dir, status) || !tools::editor_alive(status) ||
        status.value("session", Json()) != arguments["session"] ||
        status.value("runId", Json()) != arguments["runId"] || !status.value("running", false) ||
        status.value("projectRoot", Json()) != PathUtils::to_utf8(project))
    {
        return fail(result, "Project script session or active Run does not match this target");
    }
    fs::path node;
    fs::path builder;
    std::string error;
    if (!pipeline::editor_tool_path("node", ENGINE_NODE_EXECUTABLE, node, error) ||
        !pipeline::editor_tool_data_path("scripts/build_scripts.mjs", ENGINE_SCRIPT_BUILDER,
                                         builder, error))
    {
        return fail(result, std::move(error));
    }
    const auto directory = project / "cache" / (".mcp-script-" + pipeline::new_guid());
    std::error_code code;
    if (!tools::no_symlinks(directory) || !fs::create_directory(directory, code) || code)
    {
        return fail(result, "Cannot create project script compiler staging directory");
    }
    const auto input = directory / "request.json";
    const Json request = {{"sourcePath", arguments["sourcePath"]}, {"source", source}};
    std::string compiled;
    const bool success =
        pipeline::write_text(input, request.dump(-1, ' ', false, Json::error_handler_t::replace),
                             error) &&
        pipeline::run_import_tool({PathUtils::to_utf8(node), PathUtils::to_utf8(builder),
                                   "--project", PathUtils::to_utf8(project), "--snippet",
                                   PathUtils::to_utf8(input)},
                                  compiled, error) == pipeline::ImportToolStatus::success;
    fs::remove_all(directory, code);
    if (!success || code)
    {
        return fail(result, code ? "Cannot clean project script compiler staging directory"
                                 : std::move(error));
    }
    const auto output = Json::parse(compiled, nullptr, false);
    if (!output.is_object() || !output.contains("modulePath") ||
        !output["modulePath"].is_string() || !output.contains("javascript") ||
        !output["javascript"].is_string() ||
        output["javascript"].get_ref<const std::string&>().size() > script_limit)
    {
        return fail(result, "Project script compiler returned invalid JavaScript");
    }
    // 编译期间可以发生停止或重载，编辑器安全点再次核对这两个身份。
    return editor.call_command("execute_project_script",
                               {{"session", arguments["session"]},
                                {"runId", arguments["runId"]},
                                {"modulePath", output["modulePath"]},
                                {"javascript", output["javascript"]},
                                {"arguments", values}},
                               result);
}
} // namespace

bool register_project_script_tools(McpSystem& system, EditorMcpTools& editor,
                                   const fs::path& project_root, const McpTarget& target)
{
    return system.register_tool(
        {"execute_project_script",
         "Compile temporary TypeScript and call its default(args, gameplay) in the matching "
         "active Run. Imports reuse loaded project modules; result is JSON. Errors do not "
         "rollback side effects. Query the original receipt when activation is pending.",
         {{"type", "object"},
          {"properties",
           {{"session", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
            {"runId", {{"type", "string"}, {"minLength", 1}, {"maxLength", 20}}},
            {"sourcePath", {{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}},
            {"source", {{"type", "string"}, {"minLength", 1}, {"maxLength", script_limit}}},
            {"arguments", Json::object()}}},
          {"required", {"session", "runId", "sourcePath", "source"}},
          {"additionalProperties", false}},
         [&editor, project_root, target](const Json& arguments, Json& result)
         { return execute(editor, project_root, target, arguments, result); }});
}
} // namespace engine::plugins::mcp
