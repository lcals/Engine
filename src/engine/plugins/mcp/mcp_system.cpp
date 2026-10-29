#include "plugins/mcp/mcp_system.hpp"

#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "editor/asset_sync/asset_sync_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_filesystem.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

namespace engine
{
namespace
{

namespace fs = std::filesystem;
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

Json android_result(bool success, const std::string& text)
{
    auto value = Json::parse(text, nullptr, false);
    if (!value.is_object())
    {
        return tool_result({{"error", "invalid Android operation receipt"}}, true);
    }
    return tool_result(std::move(value), !success);
}

bool valid_android_target(const McpTarget& target)
{
    const auto ascii_letter = [](unsigned char value)
    { return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z'); };
    if (target.android_serial.empty() || target.android_package.empty() ||
        target.adb_executable.empty() || !target.runtime_dir.empty() ||
        target.android_package.find('.') == std::string::npos)
    {
        return false;
    }
    for (const unsigned char value : target.android_serial)
    {
        if (!ascii_letter(value) && !(value >= '0' && value <= '9') && value != '-' &&
            value != '_' && value != '.' && value != ':')
        {
            return false;
        }
    }
    bool component_start = true;
    for (const unsigned char value : target.android_package)
    {
        if (value == '.')
        {
            if (component_start)
            {
                return false;
            }
            component_start = true;
        }
        else
        {
            if ((!ascii_letter(value) && value != '_') &&
                (component_start || value < '0' || value > '9'))
            {
                return false;
            }
            component_start = false;
        }
    }
    return !component_start;
}

bool no_symlinks(const fs::path& path)
{
    std::error_code error;
    fs::path current;
    for (const auto& component : path)
    {
        current /= component;
        const auto status = fs::symlink_status(current, error);
        if (error == std::errc::no_such_file_or_directory)
        {
            error.clear();
            continue;
        }
        if (error || fs::is_symlink(status))
        {
            return false;
        }
    }
    return true;
}

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

bool read_project_status(const fs::path& runtime, Json& status)
{
    const auto path = runtime / "project-status.json";
    std::error_code error;
    const auto size = fs::file_size(path, error);
    std::string text;
    if (error || size > 64 * 1024 || !no_symlinks(path) || !ResourceSystem::read_text(path, text))
    {
        return false;
    }
    status = Json::parse(text, nullptr, false);
    return status.is_object() && status.contains("session") && status["session"].is_string() &&
           status.contains("projectRoot") && status["projectRoot"].is_string() &&
           status.contains("projectState") && status["projectState"].is_string() &&
           status.contains("updatedAtMs") && status["updatedAtMs"].is_number_integer() &&
           status.contains("lastRequestId") && status["lastRequestId"].is_string() &&
           status.contains("lastRequestSucceeded") && status["lastRequestSucceeded"].is_boolean();
}

bool editor_alive(const Json& status)
{
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    const auto time = status["updatedAtMs"].get<std::int64_t>();
    return time <= now && time >= now - 5000 && status["projectState"] != "closed";
}

Json control_project(const fs::path& runtime, const fs::path& project_root,
                     const std::string& command)
{
    const auto lock = runtime / "project-control.lock";
    std::error_code error;
    if (!no_symlinks(lock) || !fs::create_directory(lock, error))
    {
        return tool_result({{"error", "Another project control operation owns the lock"}}, true);
    }
    const auto result = [&]() -> Json
    {
        Json before;
        if (!read_project_status(runtime, before) || !editor_alive(before))
        {
            return tool_result({{"error", "No live editor session for this runtime directory"}},
                               true);
        }
        const auto canonical_project = fs::canonical(project_root, error);
        if (error || before["projectRoot"] != PathUtils::to_utf8(canonical_project))
        {
            return tool_result({{"error", "Editor session belongs to a different project"}}, true);
        }
        const auto target = runtime / "project-control.json";
        if (!no_symlinks(target))
        {
            return tool_result({{"error", "Project control path contains a symlink"}}, true);
        }
        if (fs::exists(target, error))
        {
            std::string previous_text;
            const auto previous_size = fs::file_size(target, error);
            if (error || previous_size > 16 * 1024 ||
                !ResourceSystem::read_text(target, previous_text))
            {
                return tool_result({{"error", "Cannot inspect previous project request"}}, true);
            }
            const auto previous = Json::parse(previous_text, nullptr, false);
            if (previous.is_object() && previous.contains("session") &&
                previous["session"] == before["session"] && previous.contains("requestId") &&
                (previous["requestId"] != before["lastRequestId"] ||
                 before["projectState"] == "starting" || before["projectState"] == "stopping"))
            {
                return tool_result({{"error", "Previous project request is still pending"},
                                    {"requestId", previous["requestId"]}},
                                   true);
            }
        }
        else if (error)
        {
            return tool_result({{"error", "Cannot inspect project control path"}}, true);
        }
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto request_id = std::to_string(nonce);
        const auto temporary = runtime / (".project-control-" + request_id + ".json");
        if (!no_symlinks(temporary))
        {
            return tool_result({{"error", "Invalid temporary control path"}}, true);
        }
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << Json({{"session", before["session"]},
                        {"requestId", request_id},
                        {"command", command}})
                      .dump()
               << '\n';
        output.close();
        if (!output || !SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(),
                                       PathUtils::to_utf8(target).c_str()))
        {
            fs::remove(temporary, error);
            return tool_result({{"error", "Cannot publish project request"}}, true);
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline)
        {
            Json receipt;
            if (read_project_status(runtime, receipt))
            {
                if (receipt["session"] != before["session"])
                {
                    return tool_result({{"error", "Editor session changed before completion"},
                                        {"requestId", request_id}},
                                       true);
                }
                if (receipt["lastRequestId"] == request_id &&
                    receipt["projectState"] != "starting" && receipt["projectState"] != "stopping")
                {
                    receipt["requestId"] = request_id;
                    receipt["activation"] = "completed";
                    receipt["runtimeDir"] = PathUtils::to_utf8(runtime);
                    const bool succeeded = receipt["lastRequestSucceeded"].get<bool>();
                    return tool_result(std::move(receipt), !succeeded);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return tool_result({{"error", "Project request submitted; completion is not confirmed"},
                            {"requestId", request_id},
                            {"activation", "pending"}},
                           true);
    }();
    fs::remove(lock, error);
    return result;
}

Json read_status(const fs::path& runtime)
{
    Json status = {{"running", false},
                   {"state", "offline"},
                   {"activeRevision", ""},
                   {"availableRevision", ""}};
    const auto status_path = runtime / "script-status.json";
    const auto available_path = runtime / "assets" / "available.json";
    if (!no_symlinks(status_path) || !no_symlinks(available_path))
    {
        return tool_result({{"error", "script status or available path contains symlinks"}}, true);
    }
    std::error_code error;
    std::string text;
    if (fs::exists(status_path, error))
    {
        if (!ResourceSystem::read_text(status_path, text))
        {
            return tool_result({{"error", "cannot read script runtime status"}}, true);
        }
        status = Json::parse(text, nullptr, false);
        if (!status.is_object() || !status.contains("running") || !status["running"].is_boolean())
        {
            return tool_result({{"error", "script runtime status is invalid"}}, true);
        }
        status["state"] = status["running"].get<bool>() ? "lastReportedRunning" : "offline";
    }
    else if (error)
    {
        return tool_result({{"error", "cannot inspect script runtime status"}}, true);
    }

    // 已发布文件独立于引擎是否正在 tick；activeRevision 仍来自引擎上次报告。
    status["availableRevision"] = "";
    if (fs::exists(available_path, error))
    {
        if (!ResourceSystem::read_text(available_path, text))
        {
            return tool_result({{"error", "cannot read published asset manifest"}}, true);
        }
        const auto available = Json::parse(text, nullptr, false);
        if (!available.is_object() || !available.contains("revision") ||
            !available["revision"].is_string())
        {
            return tool_result({{"error", "published asset manifest is invalid"}}, true);
        }
        status["availableRevision"] = available["revision"];
    }
    else if (error)
    {
        return tool_result({{"error", "cannot inspect published asset manifest"}}, true);
    }
    const auto project_status_path = runtime / "project-status.json";
    if (fs::exists(project_status_path, error))
    {
        Json project_status;
        if (!read_project_status(runtime, project_status))
        {
            return tool_result({{"error", "Invalid editor project status"}}, true);
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
        return tool_result({{"error", "Cannot inspect editor status"}}, true);
    }
    status["runtimeDir"] = PathUtils::to_utf8(runtime);
    status["statusSource"] =
        "availableRevision is published on disk; runtime fields are the last engine report";
    return tool_result(std::move(status));
}

Json tool_list(bool desktop)
{
    const Json empty_schema = {
        {"type", "object"}, {"properties", Json::object()}, {"additionalProperties", false}};
    Json result = {
        {"tools",
         Json::array(
             {{{"name", "sync_assets"},
               {"description", "Compile project scripts and publish a complete runtime version. "
                               "Success means available, not activated."},
               {"inputSchema", empty_schema}},
              {{"name", "set_hot_reload"},
               {"description", "Request the runtime to enable or disable engine-owned hot reload."},
               {"inputSchema",
                {{"type", "object"},
                 {"properties", {{"enabled", {{"type", "boolean"}}}}},
                 {"required", Json::array({"enabled"})},
                 {"additionalProperties", false}}}},
              {{"name", "get_status"},
               {"description",
                "Read the configured runtime's reported active and available revisions."},
               {"inputSchema", empty_schema}}})}};
    if (desktop)
    {
        result["tools"].push_back(
            {{"name", "start_project"},
             {"description",
              "Start the loaded project in the existing editor window; wait for completion."},
             {"inputSchema", empty_schema}});
        result["tools"].push_back(
            {{"name", "stop_project"},
             {"description",
              "Stop the project, release its resources, and restore the blank editor window."},
             {"inputSchema", empty_schema}});
    }
    return result;
}

} // namespace

McpSystem::McpSystem(AssetPipelineSystem& pipeline, AssetSyncSystem& sync, fs::path project_root,
                     McpTarget target)
    : pipeline_(pipeline), sync_(sync), project_root_(std::move(project_root)),
      target_(std::move(target))
{
}

McpSystem::~McpSystem() noexcept
{
    shutdown();
}

bool McpSystem::on_initialize(const EngineConfig&)
{
    if (!pipeline_.is_initialized() || !sync_.is_initialized())
    {
        std::fputs("MCP requires initialized asset pipeline and sync systems\n", stderr);
        return false;
    }
    std::error_code error;
    project_root_ = fs::absolute(project_root_, error).lexically_normal();
    if (error || !no_symlinks(project_root_))
    {
        return false;
    }
    for (const char* name : {"settings", "cache", "assets"})
    {
        if (!no_symlinks(project_root_ / name) || !fs::is_directory(project_root_ / name, error) ||
            error)
        {
            std::fputs("MCP project must contain settings, cache and assets directories\n", stderr);
            return false;
        }
    }
    if (!target_.android_serial.empty() || !target_.android_package.empty())
    {
        if (!valid_android_target(target_))
        {
            std::fputs("Android MCP requires a valid explicit serial, package and adb path; "
                       "--runtime-dir is mutually exclusive\n",
                       stderr);
            return false;
        }
        return true;
    }
    if (target_.runtime_dir.empty())
    {
        std::fputs("MCP requires an explicit local or Android target\n", stderr);
        return false;
    }
    target_.runtime_dir = fs::absolute(target_.runtime_dir, error).lexically_normal();
    if (error || !no_symlinks(target_.runtime_dir))
    {
        return false;
    }
    fs::create_directories(target_.runtime_dir, error);
    return !error;
}

void McpSystem::on_shutdown() noexcept
{
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
                         {"serverInfo", {{"name", "engine-editor-tools"}, {"version", "0.1.0"}}},
                         {"instructions", "Tools target the explicit local runtime or Android "
                                          "device/package. "
                                          "Sync publishes files; the engine owns activation."}});
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
                reply = rpc_result(id, tool_list(target_.android_serial.empty()));
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
                    const auto name = params["name"].get<std::string>();
                    Json result;
                    if (name == "sync_assets" && arguments.empty())
                    {
                        if (!pipeline_.build_scripts(project_root_))
                        {
                            result = tool_result({{"error", pipeline_.last_error()}}, true);
                        }
                        else if (!target_.android_serial.empty())
                        {
                            std::string receipt;
                            const bool success = sync_android(receipt);
                            result = android_result(success, receipt);
                        }
                        else if (!sync_.sync(project_root_ / "cache" / "assets",
                                             target_.runtime_dir))
                        {
                            result = tool_result({{"error", sync_.last_error()}}, true);
                        }
                        else
                        {
                            result = tool_result(
                                {{"availableRevision", sync_.revision()},
                                 {"runtimeDir", PathUtils::to_utf8(target_.runtime_dir)},
                                 {"activation", "pending engine decision"}});
                        }
                    }
                    else if (name == "set_hot_reload" && arguments.size() == 1 &&
                             arguments.contains("enabled") && arguments["enabled"].is_boolean())
                    {
                        const bool enabled = arguments["enabled"].get<bool>();
                        if (!target_.android_serial.empty())
                        {
                            std::string receipt;
                            const bool success = set_android_hot_reload(enabled, receipt);
                            result = android_result(success, receipt);
                        }
                        else
                        {
                            result =
                                atomic_control(target_.runtime_dir, enabled)
                                    ? tool_result({{"requestedHotReload", enabled},
                                                   {"activation", "pending engine decision"}})
                                    : tool_result({{"error", "cannot publish hot reload control"}},
                                                  true);
                        }
                    }
                    else if ((name == "start_project" || name == "stop_project") &&
                             arguments.empty())
                    {
                        result = target_.android_serial.empty()
                                     ? control_project(target_.runtime_dir, project_root_, name)
                                     : tool_result({{"error", "Project start/stop currently "
                                                              "requires a desktop editor target"}},
                                                   true);
                    }
                    else if (name == "get_status" && arguments.empty())
                    {
                        if (!target_.android_serial.empty())
                        {
                            std::string receipt;
                            const bool success = read_android_status(receipt);
                            result = android_result(success, receipt);
                        }
                        else
                        {
                            result = read_status(target_.runtime_dir);
                        }
                    }
                    else
                    {
                        reply = rpc_error(id, -32602, "Unknown tool or invalid arguments");
                    }
                    if (reply.is_null())
                    {
                        reply = rpc_result(id, std::move(result));
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
