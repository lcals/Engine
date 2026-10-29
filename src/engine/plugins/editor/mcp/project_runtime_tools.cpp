#include "plugins/editor/mcp/project_mcp_tools.hpp"

#include "plugins/editor/mcp/mcp_system.hpp"
#include "plugins/editor/mcp/project_tool_utils.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_filesystem.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace engine::plugins::mcp
{
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
using tools::editor_alive;
using tools::no_symlinks;
using tools::read_project_status;

bool atomic_control(const fs::path& runtime, bool enabled)
{
    const auto target = runtime / "script-control.json";
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporary = runtime / (".script-control-" + std::to_string(nonce) + ".json");
    if (!no_symlinks(target) || !no_symlinks(temporary))
    {
        return false;
    }
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << Json({{"hotReload", enabled}}).dump() << '\n';
    output.close();
    std::error_code error;
    if (output.fail())
    {
        fs::remove(temporary, error);
        return false;
    }
    if (!SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(), PathUtils::to_utf8(target).c_str()))
    {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
    return true;
}

bool read_status(const fs::path& runtime, Json& response)
{
    Json status = {{"running", false},
                   {"state", "offline"},
                   {"activeRevision", ""},
                   {"availableRevision", ""}};
    const auto status_path = runtime / "script-status.json";
    const auto available_path = runtime / "assets" / "available.json";
    if (!no_symlinks(status_path) || !no_symlinks(available_path))
    {
        response = {{"error", "script status or available path contains symlinks"}};
        return false;
    }
    std::error_code error;
    std::string text;
    if (fs::exists(status_path, error))
    {
        if (!ResourceSystem::read_text(status_path, text))
        {
            response = {{"error", "cannot read script runtime status"}};
            return false;
        }
        status = Json::parse(text, nullptr, false);
        if (!status.is_object() || !status.contains("running") || !status["running"].is_boolean())
        {
            response = {{"error", "script runtime status is invalid"}};
            return false;
        }
        status["state"] = status["running"].get<bool>() ? "lastReportedRunning" : "offline";
    }
    else if (error)
    {
        response = {{"error", "cannot inspect script runtime status"}};
        return false;
    }

    // 已发布文件独立于引擎是否正在 tick；activeRevision 仍来自引擎上次报告。
    status["availableRevision"] = "";
    if (fs::exists(available_path, error))
    {
        if (!ResourceSystem::read_text(available_path, text))
        {
            response = {{"error", "cannot read published asset manifest"}};
            return false;
        }
        const auto available = Json::parse(text, nullptr, false);
        if (!available.is_object() || !available.contains("revision") ||
            !available["revision"].is_string())
        {
            response = {{"error", "published asset manifest is invalid"}};
            return false;
        }
        status["availableRevision"] = available["revision"];
    }
    else if (error)
    {
        response = {{"error", "cannot inspect published asset manifest"}};
        return false;
    }
    const auto project_status_path = runtime / "project-status.json";
    if (fs::exists(project_status_path, error))
    {
        Json project_status;
        if (!read_project_status(runtime, project_status))
        {
            response = {{"error", "Invalid editor project status"}};
            return false;
        }
        for (auto item = project_status.begin(); item != project_status.end(); ++item)
        {
            status[item.key()] = item.value();
        }
        const bool alive = editor_alive(project_status);
        status["editorAlive"] = alive;
        status["state"] = alive ? "lastReportedEditor" : "offline";
        if (!alive)
        {
            status["lastReportedProjectState"] = status["projectState"];
            status["projectState"] = "offline";
            status["running"] = false;
        }
    }
    else if (error)
    {
        response = {{"error", "Cannot inspect editor status"}};
        return false;
    }
    status["runtimeDir"] = PathUtils::to_utf8(runtime);
    status["statusSource"] =
        "availableRevision is published on disk; runtime fields are the last engine report";
    response = std::move(status);
    return true;
}

} // namespace

bool ProjectMcpTools::register_runtime_tools(McpSystem& mcp)
{
    const bool control = mcp.register_tool(
        {"set_hot_reload",
         "Request the runtime to enable or disable engine-owned hot reload.",
         {{"type", "object"},
          {"properties", {{"enabled", {{"type", "boolean"}}}}},
          {"required", Json::array({"enabled"})},
          {"additionalProperties", false}},
         [this](const Json& arguments, Json& output)
         {
             const bool enabled = arguments["enabled"].get<bool>();
             if (!target_.android_serial.empty())
             {
                 std::string receipt;
                 const bool success = set_android_hot_reload(enabled, receipt);
                 return tools::android_receipt(success, receipt, output);
             }
             if (!atomic_control(target_.runtime_dir, enabled))
             {
                 output = {{"error", "cannot publish hot reload control"}};
                 return false;
             }
             output = {{"requestedHotReload", enabled}, {"activation", "pending engine decision"}};
             return true;
         }});
    if (!control)
    {
        return false;
    }
    return mcp.register_tool(
        {"get_status",
         "Read the configured runtime's reported active and available revisions.",
         {{"type", "object"}, {"properties", Json::object()}, {"additionalProperties", false}},
         [this](const Json&, Json& output)
         {
             if (!target_.android_serial.empty())
             {
                 std::string receipt;
                 const bool success = read_android_status(receipt);
                 return tools::android_receipt(success, receipt, output);
             }
             return read_status(target_.runtime_dir, output);
         }});
}

} // namespace engine::plugins::mcp
