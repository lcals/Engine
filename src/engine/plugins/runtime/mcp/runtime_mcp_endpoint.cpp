#include "plugins/runtime/mcp/runtime_mcp_endpoint.hpp"
#include "plugins/runtime/mcp/mcp_command_channel.hpp"
#include "plugins/runtime/mcp/mcp_system.hpp"
#include "plugins/runtime/mcp/runtime_values.hpp"
#include "runtime/app/runtime_host.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/gameplay/project_script_request.hpp"
#include "runtime/gameplay/script_status.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL.h>
#include <charconv>
#include <chrono>
#include <fstream>
#include <system_error>

namespace engine::plugins::mcp
{
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
bool plain_path(const fs::path& path)
{
    fs::path current;
    for (const auto& part : path)
    {
        current /= part;
        std::error_code error;
        const auto status = fs::symlink_status(current, error);
        if (error == std::errc::no_such_file_or_directory)
        {
            continue;
        }
        if (error || fs::is_symlink(status))
        {
            return false;
        }
    }
    return true;
}

} // namespace

struct RuntimeMcpEndpoint::State
{
    RuntimeHost& host;
    fs::path cache;
    fs::path directory;
    std::string session;
    std::string last_error;
    std::future<bool> status_write;
    std::uint64_t snapshot_sequence = 0;
    std::chrono::steady_clock::time_point last_write{};
    McpSystem system;
    std::unique_ptr<McpCommandChannel> channel;
    bool closed = false;
    explicit State(RuntimeHost& value) : host(value)
    {
    }

    Json status_value()
    {
        auto* gameplay = closed ? nullptr : host.gameplay();
        const auto* script = gameplay ? gameplay->script_status() : nullptr;
        RuntimeHostStatus status;
        (void)host.status(status);
        GameplayPrepareSnapshot preparation;
        const bool preparing = gameplay && status.prepare_id != 0 &&
                               gameplay->prepare_status(status.prepare_id, preparation) &&
                               (preparation.state == GameplayPrepareStatus::preparing ||
                                preparation.state == GameplayPrepareStatus::ready);
        const bool running = !preparing && script && script->running && script->run_id != 0;
        Json result = {{"snapshotId", std::to_string(++snapshot_sequence)},
                       {"hostKind", "runtime-test"},
                       {"session", session},
                       {"projectRoot", PathUtils::to_utf8(cache.parent_path())},
                       {"projectState", closed      ? "closed"
                                        : preparing ? "starting"
                                        : running   ? "running"
                                                    : "idle"},
                       {"running", running},
                       {"runId", std::to_string(running ? script->run_id : 0)},
                       {"activeRevision", script ? script->active_revision : ""},
                       {"tickId", std::to_string(status.tick_id)},
                       {"sampledAtNs", std::to_string(status.sampled_at_ns)},
                       {"windowDrawable", status.window_drawable},
                       {"initialized", status.initialized},
                       {"prepareId", std::to_string(status.prepare_id)},
                       {"hostErrorCode", status.error_code},
                       {"hostError", status.error},
                       {"lastError", status.error.empty() ? last_error : status.error},
                       {"lastRequestId", ""},
                       {"lastCommand", ""},
                       {"lastRequestSucceeded", false},
                       {"capabilities",
                        {{"sceneLoading", gameplay != nullptr},
                         {"frameCapture", host.renderer() != nullptr},
                         {"scriptExecution", script != nullptr}}},
                       {"loadIds", Json::array()}};
        if (channel && channel->last_receipt().is_object())
        {
            const auto& receipt = channel->last_receipt();
            result["lastRequestId"] = receipt.value("requestId", "");
            result["lastCommand"] = receipt.value("command", "");
            result["lastRequestSucceeded"] = receipt.value("success", false);
            result["lastCommandError"] = receipt.value("error", "");
        }
        if (gameplay)
        {
            std::vector<SceneLoadId> loads;
            if (gameplay->collect_scene_loads(loads))
            {
                for (const auto& id : loads)
                {
                    result["loadIds"].push_back({{"owner", std::to_string(id.owner)},
                                                 {"request", std::to_string(id.request)}});
                }
            }
        }
        RenderFrameSnapshot frame;
        if (!closed && host.renderer() && host.renderer()->frame_status({}, frame))
        {
            result["frame"] = frame_value(frame);
        }
        else
        {
            result["frame"] = nullptr;
        }
        return result;
    }

    bool publish(bool force)
    {
        auto status = status_value();
        const auto now = std::chrono::steady_clock::now();
        if (!force && now - last_write < std::chrono::milliseconds(100))
        {
            return true;
        }
        status["updatedAtMs"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
        const auto target = directory / "project-status.json";
        if (!plain_path(target))
        {
            return false;
        }
        if (!publish_observation(host.tasks(), status_write, target, status, force))
        {
            return false;
        }
        last_write = now;
        return true;
    }
    bool execute_script(const Json& arguments, Json& result, std::string& error)
    {
        if (closed || !arguments.is_object() || arguments.size() != 5 ||
            arguments.value("session", Json()) != session || !arguments.contains("runId") ||
            !arguments["runId"].is_string() || !arguments.contains("modulePath") ||
            !arguments["modulePath"].is_string() || !arguments.contains("javascript") ||
            !arguments["javascript"].is_string() || !arguments.contains("arguments"))
        {
            error = "Project script requires the current runtime test session";
            return false;
        }
        const auto& run_text = arguments["runId"].get_ref<const std::string&>();
        std::uint64_t run = 0;
        const auto parsed =
            std::from_chars(run_text.data(), run_text.data() + run_text.size(), run);
        auto* gameplay = host.gameplay();
        const auto* current = gameplay == nullptr ? nullptr : gameplay->script_status();
        if (parsed.ec != std::errc{} || parsed.ptr != run_text.data() + run_text.size() ||
            run == 0 || current == nullptr || !current->running || current->run_id != run)
        {
            error = "Project script session or active Run does not match this runtime";
            return false;
        }
        const auto values =
            arguments["arguments"].dump(-1, ' ', false, Json::error_handler_t::replace);
        const ProjectScriptRequest request{
            run, arguments["modulePath"].get_ref<const std::string&>(),
            arguments["javascript"].get_ref<const std::string&>(), values};
        std::string output;
        if (!gameplay->execute_project_script(request, output, error))
        {
            return false;
        }
        auto value = Json::parse(output, nullptr, false);
        if (value.is_discarded())
        {
            error = "Project script returned invalid JSON";
            return false;
        }
        result = {{"runId", run_text}, {"value", std::move(value)}};
        return true;
    }
};

RuntimeMcpEndpoint::RuntimeMcpEndpoint() noexcept = default;
RuntimeMcpEndpoint::~RuntimeMcpEndpoint() noexcept
{
    shutdown();
}

bool RuntimeMcpEndpoint::initialize(RuntimeHost& host, const McpEndpointConfig& config)
{
    if (state_ || config.cache_root.empty() || config.channel_directory.empty() ||
        !plain_path(config.cache_root) || !plain_path(config.channel_directory))
    {
        error_ = "Runtime MCP requires independent cache and channel paths";
        return false;
    }
    auto state = std::make_unique<State>(host);
    std::error_code code;
    fs::create_directories(config.cache_root, code);
    if (code)
    {
        return false;
    }
    state->cache = fs::canonical(config.cache_root, code);
    if (code)
    {
        return false;
    }
    fs::create_directories(config.channel_directory, code);
    if (code)
    {
        return false;
    }
    state->directory = fs::canonical(config.channel_directory, code);
    if (code || state->directory == state->cache ||
        (!state->directory.lexically_relative(state->cache).empty() &&
         state->directory.lexically_relative(state->cache).begin()->string() == ".."))
    {
        error_ = "Runtime MCP channel must be inside its cache root";
        return false;
    }
    state->session =
        config.session.empty() ? std::to_string(SDL_GetPerformanceCounter()) : config.session;
    auto* value = state.get();
    const auto register_command = [&](std::string name, auto handler)
    {
        McpTool tool;
        tool.name = std::move(name);
        tool.description = "Runtime test operation";
        tool.input_schema = {{"type", "object"},
                             {"properties", Json::object()},
                             {"required", Json::array()},
                             {"additionalProperties", true}};
        tool.execute = [handler](const Json& args, Json& result)
        {
            std::string error;
            if (handler(args, result, error))
            {
                return true;
            }
            result = {{"error", error.empty() ? "Runtime test operation failed" : error}};
            return false;
        };
        return value->system.register_tool(std::move(tool));
    };
    if (!register_command("execute_project_script",
                          [value](const Json& args, Json& result, std::string& error)
                          { return value->execute_script(args, result, error); }) ||
        !register_command("capture_frame",
                          [value](const Json& args, Json& result, std::string& error)
                          {
                              return observation_command(value->host, value->cache, "capture_frame",
                                                         args, result, error);
                          }) ||
        !register_command("get_frame_capture",
                          [value](const Json& args, Json& result, std::string& error)
                          {
                              return observation_command(value->host, value->cache,
                                                         "get_frame_capture", args, result, error);
                          }) ||
        !register_command("get_scene_load",
                          [value](const Json& args, Json& result, std::string& error)
                          {
                              return observation_command(value->host, value->cache,
                                                         "get_scene_load", args, result, error);
                          }) ||
        !register_command("get_gameplay_prepare",
                          [value](const Json& args, Json& result, std::string& error)
                          {
                              return observation_command(value->host, value->cache,
                                                         "get_gameplay_prepare", args, result,
                                                         error);
                          }) ||
        !register_command("release_gameplay_prepare",
                          [value](const Json& args, Json& result, std::string& error)
                          {
                              return observation_command(value->host, value->cache,
                                                         "release_gameplay_prepare", args, result,
                                                         error);
                          }) ||
        !value->system.initialize({}))
    {
        return false;
    }
    state->channel = std::make_unique<McpCommandChannel>(
        state->directory, state->session,
        [value](std::string_view name, const Json& args, Json& result, std::string& error)
        {
            const auto status = value->system.call_tool(name, args, result);
            if (status == McpCallStatus::success)
            {
                return true;
            }
            error = result.is_object() ? result.value("error", "Runtime operation is unavailable")
                                       : "Runtime operation is unavailable";
            return false;
        });
    if (!state->publish(false))
    {
        return false;
    }
    state_ = std::move(state);
    return true;
}

bool RuntimeMcpEndpoint::poll()
{
    if (!state_ || state_->closed)
    {
        return false;
    }
    if (!state_->channel->poll())
    {
        error_ = state_->channel->last_error();
        return false;
    }
    return publish_status();
}
bool RuntimeMcpEndpoint::publish_status()
{
    return state_ && state_->publish(false);
}
void RuntimeMcpEndpoint::shutdown() noexcept
{
    if (state_)
    {
        state_->closed = true;
        (void)state_->publish(true);
        state_.reset();
    }
}
std::string_view RuntimeMcpEndpoint::last_error() const noexcept
{
    return error_;
}
} // namespace engine::plugins::mcp
