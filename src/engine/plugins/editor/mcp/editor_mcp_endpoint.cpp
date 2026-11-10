#include "plugins/editor/mcp/editor_mcp_endpoint.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/editor_system.hpp"
#include "plugins/runtime/mcp/mcp_command_channel.hpp"
#include "plugins/runtime/mcp/runtime_values.hpp"
#include "runtime/app/runtime_host.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL.h>
#include <chrono>
#include <fstream>
#include <optional>

namespace engine::plugins::mcp
{
using Json = nlohmann::json;
namespace fs = std::filesystem;
struct EditorMcpEndpoint::State
{
    editor::EditorSystem& editor;
    RuntimeHost& host;
    McpEndpointConfig config;
    std::unique_ptr<McpCommandChannel> channel;
    std::optional<std::chrono::steady_clock::time_point> close_at;
    std::uint32_t close_delay = 0;
    std::future<bool> status_write;
    std::chrono::steady_clock::time_point last_write{};
    std::uint64_t snapshot_sequence = 0;
    Json last_status;
    State(editor::EditorSystem& value, RuntimeHost& runtime) : editor(value), host(runtime)
    {
    }
};
EditorMcpEndpoint::EditorMcpEndpoint() noexcept = default;
EditorMcpEndpoint::~EditorMcpEndpoint() noexcept
{
    shutdown();
}

bool EditorMcpEndpoint::initialize(editor::EditorSystem& editor, RuntimeHost& host,
                                   const McpEndpointConfig& config)
{
    if (state_ || !editor.commands())
    {
        return false;
    }
    auto state = std::make_unique<State>(editor, host);
    state->config = config;
    Json initial;
    if (!editor.commands()->execute("get_project_status", Json::object(), initial, error_))
    {
        return false;
    }
    if (!initial.is_object() || !initial.contains("session") || !initial["session"].is_string())
    {
        error_ = "Editor status session is unavailable";
        return false;
    }
    state->config.session = initial["session"].get<std::string>();
    auto* value = state.get();
    state->channel = std::make_unique<McpCommandChannel>(
        config.channel_directory, state->config.session,
        [value](std::string_view command, const Json& args, Json& result, std::string& error)
        {
            if (command == "capture_frame" || command == "get_frame_capture" ||
                command == "get_scene_load")
            {
                return observation_command(value->host, value->config.cache_root, command, args,
                                           result, error);
            }
            if (command == "close_editor")
            {
                if (!args.is_object() || args.size() > 1 ||
                    (args.size() == 1 &&
                     (!args.contains("delayMs") || !args["delayMs"].is_number_integer() ||
                      args["delayMs"] < 100 || args["delayMs"] > 10000)))
                {
                    error = "close_editor accepts delayMs within 100..10000";
                    return false;
                }
                if (value->close_delay == 0)
                {
                    value->close_delay = args.value("delayMs", 500U);
                }
                result = {{"scheduled", true}, {"delayMs", value->close_delay}};
                return true;
            }
            return value->editor.commands() &&
                   value->editor.commands()->execute(command, args, result, error);
        });
    state_ = std::move(state);
    return publish_status();
}

bool EditorMcpEndpoint::poll()
{
    if (!state_ || !state_->channel->poll())
    {
        if (state_)
        {
            error_ = state_->channel->last_error();
        }
        return false;
    }
    const auto& receipt = state_->channel->last_receipt();
    if (state_->close_delay && receipt.is_object() &&
        receipt.value("command", "") == "close_editor" && receipt.value("success", false))
    {
        if (!state_->close_at)
        {
            state_->close_at =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(state_->close_delay);
        }
        if (std::chrono::steady_clock::now() >= *state_->close_at)
        {
            SDL_Event event{};
            event.type = SDL_EVENT_QUIT;
            if (!SDL_PushEvent(&event))
            {
                error_ = "Cannot request editor exit";
                return false;
            }
            state_->close_delay = 0;
        }
    }
    return publish_status();
}

bool EditorMcpEndpoint::publish_status()
{
    if (!state_)
    {
        return false;
    }
    const auto target = state_->config.channel_directory / "project-status.json";
    const auto now = std::chrono::steady_clock::now();
    if (now - state_->last_write < std::chrono::milliseconds(100))
    {
        return true;
    }
    Json status;
    if (!state_->editor.commands() ||
        !state_->editor.commands()->execute("get_project_status", Json::object(), status, error_))
    {
        return false;
    }
    if (!status.is_object() || status.value("session", "") != state_->config.session)
    {
        return false;
    }
    RenderFrameSnapshot frame;
    status["frame"] = state_->host.renderer() && state_->host.renderer()->frame_status({}, frame)
                          ? frame_value(frame)
                          : Json(nullptr);
    RuntimeHostStatus host;
    (void)state_->host.status(host);
    status["snapshotId"] = std::to_string(++state_->snapshot_sequence);
    status["sampledAtNs"] = std::to_string(host.sampled_at_ns);
    status["updatedAtMs"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
    status["capabilities"] = {
        {"sceneLoading", state_->host.gameplay() != nullptr},
        {"frameCapture", state_->host.renderer() != nullptr},
#if defined(ENGINE_ENABLE_TEST_SUPPORT)
        {"scriptExecution", state_->host.gameplay() && state_->host.gameplay()->script_status()}
#else
        {"scriptExecution", false}
#endif
    };
    status["tickId"] = std::to_string(host.tick_id);
    status["windowDrawable"] = host.window_drawable;
    status["loadIds"] = Json::array();
    if (state_->host.gameplay())
    {
        std::vector<SceneLoadId> ids;
        if (state_->host.gameplay()->collect_scene_loads(ids))
        {
            for (auto id : ids)
            {
                status["loadIds"].push_back(
                    {{"owner", std::to_string(id.owner)}, {"request", std::to_string(id.request)}});
            }
        }
    }
    const auto& receipt = state_->channel->last_receipt();
    if (receipt.is_object())
    {
        status["lastRequestId"] = receipt.value("requestId", "");
        status["lastCommand"] = receipt.value("command", "");
        status["lastRequestSucceeded"] = receipt.value("success", false);
        status["lastCommandError"] = receipt.value("error", "");
    }
    state_->last_write = now;
    state_->last_status = status;
    return publish_observation(state_->host.tasks(), state_->status_write, target, status, false);
}
void EditorMcpEndpoint::shutdown() noexcept
{
    if (!state_)
    {
        return;
    }
    auto status = state_->last_status;
    if (status.is_object())
    {
        status["projectState"] = "closed";
        status["running"] = false;
        status["runId"] = "0";
        status["windowDrawable"] = false;
        status["frame"] = nullptr;
        status["loadIds"] = Json::array();
        status["capabilities"] = {
            {"sceneLoading", false}, {"frameCapture", false}, {"scriptExecution", false}};
        status["updatedAtMs"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
        (void)publish_observation(state_->host.tasks(), state_->status_write,
                                  state_->config.channel_directory / "project-status.json", status,
                                  true);
    }
    else if (state_->status_write.valid())
    {
        (void)state_->status_write.get();
    }
    state_.reset();
}

std::string_view EditorMcpEndpoint::last_error() const noexcept
{
    return error_;
}
} // namespace engine::plugins::mcp
