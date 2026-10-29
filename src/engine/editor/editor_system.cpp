#include "editor/editor_system.hpp"

#include "editor/editor_environment.hpp"
#include "runtime/app/app.hpp"
#include "runtime/engine.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/script/script_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace engine
{
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

std::int64_t timestamp_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool plain_path(const fs::path& path)
{
    std::error_code error;
    fs::path current;
    for (const auto& part : path)
    {
        current /= part;
        const auto state = fs::symlink_status(current, error);
        if (error == std::errc::no_such_file_or_directory)
        {
            error.clear();
            continue;
        }
        if (error || fs::is_symlink(state))
        {
            return false;
        }
    }
    return true;
}
} // namespace

struct EditorSystem::State
{
    Engine& runtime;
    fs::path directory;
    std::string project_root;
    std::string session;
    std::string last_control;
    std::string last_request;
    std::string last_command;
    std::string last_error;
    bool last_success = true;
    bool closed = false;
    std::string transition;
    std::string previous_status;
    std::chrono::steady_clock::time_point last_write{};

    explicit State(Engine& engine) : runtime(engine)
    {
    }

    bool publish(bool force = false)
    {
        const auto* script = runtime.script_status();
        const bool running = !closed && script != nullptr && script->running;
        std::string error = last_error;
        if (error.empty())
        {
            error = runtime.last_error();
        }
        if (error.empty() && script != nullptr)
        {
            error = script->last_error;
        }
        Json status = {{"session", session},
                       {"projectRoot", project_root},
                       {"projectState", closed               ? "closed"
                                        : transition.empty() ? (running ? "running" : "idle")
                                                             : transition},
                       {"running", running},
                       {"lastRequestId", last_request},
                       {"lastCommand", last_command},
                       {"lastRequestSucceeded", last_success},
                       {"lastError", error}};
        const auto serialized = status.dump(-1, ' ', false, Json::error_handler_t::replace);
        const auto now = std::chrono::steady_clock::now();
        if (!force && serialized == previous_status &&
            now - last_write < std::chrono::milliseconds(250))
        {
            return true;
        }
        status["updatedAtMs"] = timestamp_ms();
        const auto target = directory / "project-status.json";
        const auto temporary = directory / (".project-status-" + session + ".json");
        if (!plain_path(target) || !plain_path(temporary))
        {
            return false;
        }
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << status.dump(-1, ' ', false, Json::error_handler_t::replace) << '\n';
        output.close();
        if (!output || !SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(),
                                       PathUtils::to_utf8(target).c_str()))
        {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return false;
        }
        previous_status = serialized;
        last_write = now;
        return true;
    }

    bool poll()
    {
        const auto control = directory / "project-control.json";
        std::error_code error;
        if (!plain_path(control))
        {
            last_error = "Project control path contains a symlink";
            return publish();
        }
        const bool exists = fs::exists(control, error);
        if (error)
        {
            last_error = "Cannot inspect project control";
            return publish();
        }
        if (!exists)
        {
            return publish();
        }
        std::string text;
        const auto size = fs::file_size(control, error);
        if (error || size > 16 * 1024 || !ResourceSystem::read_text(control, text))
        {
            last_error = "Cannot read bounded project control";
            return publish();
        }
        if (text == last_control)
        {
            return publish();
        }
        last_control = text;
        const auto request = Json::parse(text, nullptr, false);
        if (!request.is_object() || !request.contains("session") ||
            !request["session"].is_string() || !request.contains("requestId") ||
            !request["requestId"].is_string() || !request.contains("command") ||
            !request["command"].is_string())
        {
            last_error = "Invalid project control";
            return publish();
        }
        // 旧编辑器会话的控制文件不能在新会话自动执行。
        if (request["session"] != session)
        {
            return publish();
        }
        const auto request_id = request["requestId"].get<std::string>();
        if (request_id.empty() || request_id.size() > 128 || request_id == last_request)
        {
            return publish();
        }
        last_request = request_id;
        last_command = request["command"].get<std::string>();
        last_error.clear();
        last_success = false;
        if (last_command == "start_project" || last_command == "stop_project")
        {
            transition = last_command == "start_project" ? "starting" : "stopping";
            if (!publish(true))
            {
                transition.clear();
                return false;
            }
            if (last_command == "start_project")
            {
                last_success = runtime.start_gameplay();
                if (!last_success)
                {
                    last_error = runtime.last_error();
                }
            }
            else
            {
                runtime.stop_gameplay();
                last_success = true;
            }
            transition.clear();
        }
        else
        {
            last_error = "Unknown project control command";
        }
        LogSystem::write(last_success ? LogLevel::info : LogLevel::error,
                         "Project control " + last_command + ": " +
                             (last_success ? "completed" : last_error));
        return publish(true);
    }
};

EditorSystem::EditorSystem(EditorEnvironment& environment, Engine* runtime) noexcept
    : environment_(environment), runtime_(runtime)
{
}

EditorSystem::~EditorSystem() noexcept
{
    shutdown();
}

bool EditorSystem::on_initialize(const EngineConfig&)
{
    if (!environment_.is_initialized() || !IMGUI_CHECKVERSION())
    {
        return false;
    }
    ImGuiContext* previous = ImGui::GetCurrentContext();
    context_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(previous);
    if (context_ == nullptr)
    {
        return false;
    }
    if (runtime_ != nullptr)
    {
        if (!App::is_initialized())
        {
            return false;
        }
        state_ = std::make_unique<State>(*runtime_);
        state_->directory = App::persistent_data_path();
        if (!plain_path(state_->directory))
        {
            return false;
        }
        state_->project_root = environment_.project_identity_;
        state_->session =
            std::to_string(timestamp_ms()) + "-" + std::to_string(SDL_GetPerformanceCounter());
        if (!state_->publish(true))
        {
            return false;
        }
    }
    return true;
}

bool EditorSystem::on_tick(double)
{
    return environment_.is_initialized() && (state_ == nullptr || state_->poll());
}

void EditorSystem::on_shutdown() noexcept
{
    if (state_ != nullptr)
    {
        runtime_->stop_gameplay();
        state_->closed = true;
        if (!state_->session.empty() && !state_->publish(true))
        {
            LogSystem::write(LogLevel::error, "Cannot publish editor shutdown status");
        }
        state_.reset();
    }
    if (context_ != nullptr)
    {
        ImGui::DestroyContext(context_);
        context_ = nullptr;
    }
}
} // namespace engine
