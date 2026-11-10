#include "plugins/editor/mcp/runtime_mcp_bridge.hpp"

#include "editor/commands/editor_command_channel.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/editor_edit.hpp"
#include "runtime/app/runtime_host.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/gameplay/project_script_request.hpp"
#include "runtime/gameplay/script_status.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_filesystem.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <memory>
#include <system_error>
#include <utility>

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

bool within(const fs::path& parent, const fs::path& child)
{
    const auto result = std::mismatch(parent.begin(), parent.end(), child.begin(), child.end());
    return result.first == parent.end() && result.second != child.end();
}

const char* capture_state(ScreenshotStatus status)
{
    switch (status)
    {
    case ScreenshotStatus::pending:
        return "pending";
    case ScreenshotStatus::complete:
        return "complete";
    case ScreenshotStatus::failed:
        return "failed";
    default:
        return "idle";
    }
}

const char* preparation_state(RenderPrepareStatus status)
{
    switch (status)
    {
    case RenderPrepareStatus::preparing:
        return "preparing";
    case RenderPrepareStatus::ready:
        return "ready";
    case RenderPrepareStatus::failed:
        return "failed";
    default:
        return "idle";
    }
}
} // namespace

struct RuntimeMcpBridge::State
{
    RuntimeHost& host;
    fs::path project;
    fs::path cache;
    fs::path directory;
    fs::path capture_path;
    std::string session;
    std::string previous_status;
    std::string last_request;
    std::string last_command;
    std::string last_error;
    bool last_success = false;
    bool closed = false;
    std::chrono::steady_clock::time_point last_write{};
    editor::EditorCommands commands{[] { return std::uint64_t{0}; }};
    std::unique_ptr<editor::EditorCommandChannel> channel;

    explicit State(RuntimeHost& value) : host(value)
    {
    }

    bool publish(bool force)
    {
        const auto* gameplay = closed ? nullptr : host.gameplay();
        const auto* script = gameplay == nullptr ? nullptr : gameplay->script_status();
        const bool running = script != nullptr && script->running && script->run_id != 0;
        Json status = {{"hostKind", "runtime-test"},
                       {"session", session},
                       {"projectRoot", PathUtils::to_utf8(project)},
                       {"projectState", closed ? "closed" : running ? "running" : "idle"},
                       {"running", running},
                       {"runId", std::to_string(running ? script->run_id : 0)},
                       {"activeRevision", script == nullptr ? "" : script->active_revision},
                       {"lastRequestId", last_request},
                       {"lastCommand", last_command},
                       {"lastRequestSucceeded", last_success},
                       {"lastError", last_error}};
        RenderPrepareProgress progress;
        const auto* renderer = closed ? nullptr : host.renderer();
        const auto preparation = renderer ? renderer->preparation_status(progress)
                                          : RenderPrepareStatus::idle;
        status["renderPreparation"] = {{"state", preparation_state(preparation)},
                                       {"generation", progress.generation},
                                       {"discoveryComplete", progress.discovery_complete},
                                       {"resourcesReady", progress.resources_ready},
                                       {"resourcesTotal", progress.resources_total},
                                       {"stagingBytes", progress.staging_bytes}};
        const auto serialized = status.dump(-1, ' ', false, Json::error_handler_t::replace);
        const auto now = std::chrono::steady_clock::now();
        if (!force && serialized == previous_status &&
            now - last_write < std::chrono::milliseconds(250))
        {
            return true;
        }
        status["updatedAtMs"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
        const auto destination = directory / "project-status.json";
        const auto temporary = directory / (".project-status-" + session + ".json");
        if (!plain_path(destination) || !plain_path(temporary))
        {
            last_error = "Runtime status path contains a symlink or is inaccessible";
            return false;
        }
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << status.dump(-1, ' ', false, Json::error_handler_t::replace) << '\n';
        output.close();
        if (!output || !SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(),
                                       PathUtils::to_utf8(destination).c_str()))
        {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            last_error = "Cannot publish runtime test status";
            return false;
        }
        previous_status = serialized;
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
        const auto parsed = std::from_chars(run_text.data(), run_text.data() + run_text.size(), run);
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

    bool capture(const Json& arguments, Json& result, std::string& error)
    {
        auto* renderer = host.renderer();
        if (closed || renderer == nullptr || !arguments.is_object() || arguments.size() != 1 ||
            !arguments.contains("path") || !arguments["path"].is_string())
        {
            error = "Frame capture requires a renderer and a relative cache path";
            return false;
        }
        const auto& text = arguments["path"].get_ref<const std::string&>();
        const auto relative = PathUtils::from_utf8(text);
        if (text.empty() || text.find('\0') != std::string::npos ||
            text.find('\\') != std::string::npos || relative.is_absolute() ||
            relative.has_root_name() || relative.extension() != ".png")
        {
            error = "Screenshot path must be a relative .png filename";
            return false;
        }
        for (const auto& part : relative)
        {
            if (part == "." || part == "..")
            {
                error = "Screenshot path cannot traverse directories";
                return false;
            }
        }
        const auto destination = cache / relative;
        if (!plain_path(destination))
        {
            error = "Screenshot path contains a symlink or inaccessible entry";
            return false;
        }
        std::error_code code;
        fs::create_directories(destination.parent_path(), code);
        if (code || !renderer->request_screenshot(PathUtils::to_utf8(destination)))
        {
            error = "Renderer cannot accept this screenshot request";
            return false;
        }
        capture_path = destination;
        result = {{"state", "pending"}, {"path", PathUtils::to_utf8(capture_path)}};
        return true;
    }
};

RuntimeMcpBridge::RuntimeMcpBridge() noexcept = default;
RuntimeMcpBridge::~RuntimeMcpBridge() noexcept
{
    shutdown();
}

bool RuntimeMcpBridge::initialize(RuntimeHost& host, const fs::path& project,
                                  const fs::path& directory)
{
    if (state_ != nullptr || project.empty() || directory.empty() || !plain_path(project) ||
        !plain_path(directory))
    {
        error_ = "Runtime test requires explicit non-symlink project and channel paths";
        return false;
    }
    std::error_code code;
    auto state = std::make_unique<State>(host);
    state->project = fs::canonical(project, code);
    if (code || !fs::is_directory(state->project / "assets", code) || code ||
        !fs::is_directory(state->project / "settings", code) || code)
    {
        error_ = "Runtime test project assets or settings are unavailable";
        return false;
    }
    state->cache = fs::canonical(state->project / "cache", code);
    if (code || !plain_path(state->project / "cache"))
    {
        error_ = "Runtime test project cache is unavailable";
        return false;
    }
    state->directory = fs::canonical(directory, code);
    if (code || !within(state->cache, state->directory) ||
        !fs::is_directory(state->directory, code) || code || !fs::is_empty(state->directory, code) ||
        code || !fs::create_directory(state->directory / ".runtime-mcp-owner", code) || code)
    {
        error_ = "Runtime MCP channel requires a new empty directory inside the project cache";
        return false;
    }
    state->session = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto* borrowed = state.get();
    using HandlerEdit = std::unique_ptr<editor::EditorEdit>&;
    if (!state->commands.register_command(
            "execute_project_script", false,
            [borrowed](const Json& args, Json& result, HandlerEdit, std::string& error)
            { return borrowed->execute_script(args, result, error); }) ||
        !state->commands.register_command(
            "capture_frame", false,
            [borrowed](const Json& args, Json& result, HandlerEdit, std::string& error)
            { return borrowed->capture(args, result, error); }) ||
        !state->commands.register_command(
            "get_frame_capture", false,
            [borrowed](const Json& args, Json& result, HandlerEdit, std::string& error)
            {
                auto* renderer = borrowed->host.renderer();
                if (!args.is_object() || !args.empty() || renderer == nullptr || borrowed->closed)
                {
                    error = "Frame capture status requires an available renderer and no arguments";
                    return false;
                }
                result = {{"state", capture_state(renderer->screenshot_status())},
                          {"path", PathUtils::to_utf8(borrowed->capture_path)}};
                return true;
            }))
    {
        error_ = "Cannot register runtime test commands";
        return false;
    }
    state->channel = std::make_unique<editor::EditorCommandChannel>(
        state->commands, state->directory, PathUtils::to_utf8(state->project), state->session);
    if (!state->publish(true))
    {
        error_ = state->last_error;
        return false;
    }
    state_ = std::move(state);
    return true;
}

bool RuntimeMcpBridge::poll()
{
    if (state_ == nullptr || state_->closed)
    {
        error_ = "Runtime MCP test channel is not active";
        return false;
    }
    if (!state_->channel->poll())
    {
        error_ = state_->channel->last_error();
        return false;
    }
    const auto& receipt = state_->channel->last_receipt();
    if (receipt.is_object() && receipt.value("requestId", std::string{}) != state_->last_request)
    {
        state_->last_request = receipt.value("requestId", std::string{});
        state_->last_command = receipt.value("command", std::string{});
        state_->last_success = receipt.value("success", false);
        state_->last_error = receipt.value("error", std::string{});
    }
    if (!state_->publish(false))
    {
        error_ = state_->last_error;
        return false;
    }
    return true;
}

void RuntimeMcpBridge::shutdown() noexcept
{
    if (state_ != nullptr)
    {
        state_->closed = true;
        if (!state_->publish(true))
        {
            error_ = state_->last_error;
        }
        state_.reset();
    }
}

std::string_view RuntimeMcpBridge::last_error() const noexcept
{
    return error_;
}
} // namespace engine::plugins::mcp
