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
    status["runtimeDir"] = PathUtils::to_utf8(runtime);
    status["statusSource"] =
        "availableRevision is published on disk; runtime fields are the last engine report";
    return tool_result(std::move(status));
}

Json tool_list()
{
    const Json empty_schema = {
        {"type", "object"}, {"properties", Json::object()}, {"additionalProperties", false}};
    return {
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
}

} // namespace

McpSystem::McpSystem(AssetPipelineSystem& pipeline, AssetSyncSystem& sync, fs::path project_root,
                     fs::path runtime_dir)
    : pipeline_(pipeline), sync_(sync), project_root_(std::move(project_root)),
      runtime_dir_(std::move(runtime_dir))
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
        if (!fs::is_directory(project_root_ / name, error) || error)
        {
            std::fputs("MCP project must contain settings, cache and assets directories\n", stderr);
            return false;
        }
    }
    runtime_dir_ = fs::absolute(runtime_dir_, error).lexically_normal();
    if (error || !no_symlinks(runtime_dir_))
    {
        return false;
    }
    fs::create_directories(runtime_dir_, error);
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
                         {"instructions", "Tools target the explicit --project and --runtime-dir. "
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
                reply = rpc_result(id, tool_list());
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
                        else if (!sync_.sync(project_root_ / "cache" / "assets", runtime_dir_))
                        {
                            result = tool_result({{"error", sync_.last_error()}}, true);
                        }
                        else
                        {
                            result = tool_result({{"availableRevision", sync_.revision()},
                                                  {"runtimeDir", PathUtils::to_utf8(runtime_dir_)},
                                                  {"activation", "pending engine decision"}});
                        }
                    }
                    else if (name == "set_hot_reload" && arguments.size() == 1 &&
                             arguments.contains("enabled") && arguments["enabled"].is_boolean())
                    {
                        const bool enabled = arguments["enabled"].get<bool>();
                        result = atomic_control(runtime_dir_, enabled)
                                     ? tool_result({{"requestedHotReload", enabled},
                                                    {"activation", "pending engine decision"}})
                                     : tool_result({{"error", "cannot publish hot reload control"}},
                                                   true);
                    }
                    else if (name == "get_status" && arguments.empty())
                    {
                        result = read_status(runtime_dir_);
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
