#include "editor/editor_system.hpp"

#include "editor/commands/editor_command_channel.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/scene_commands.hpp"
#include "editor/editor_environment.hpp"
#include "editor/scene/scene_preview.hpp"

#if ENGINE_HAS_ASSET_PIPELINE
#include "editor/asset_pipeline/project_lock.hpp"
#endif
#include "runtime/app/app.hpp"
#include "runtime/engine.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/gameplay/script_status.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>

namespace engine::editor
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

bool read_frame_rate(const fs::path& project_root, std::int32_t& target)
{
    target = 60;
    const auto path = project_root / "settings" / "editor.toml";
    std::error_code error;
    const bool exists = fs::exists(path, error);
    if (error)
    {
        LogSystem::write(LogLevel::error, "Cannot inspect editor settings: " + error.message());
        return false;
    }
    if (!exists)
    {
        return true;
    }
    std::string source;
    if (!ResourceSystem::read_text(path, source))
    {
        LogSystem::write(LogLevel::error, "Cannot read editor settings: " +
                                              PathUtils::to_utf8(path));
        return false;
    }
    const auto document = toml::parse(source);
    if (!document)
    {
        LogSystem::write(LogLevel::error, "Invalid editor settings: " +
                                              std::string(document.error().description()));
        return false;
    }
    const auto editor = document["editor"];
    if (!editor)
    {
        return true;
    }
    if (!editor.is_table())
    {
        LogSystem::write(LogLevel::error, "Editor settings require an [editor] table");
        return false;
    }
    const auto setting = editor["target_frame_rate"];
    if (!setting)
    {
        return true;
    }
    const auto value = setting.value_exact<std::int64_t>();
    if (!value || (*value != -1 && *value <= 0) ||
        *value > std::numeric_limits<std::int32_t>::max())
    {
        LogSystem::write(LogLevel::error,
                         "Editor target_frame_rate must be -1 or a positive 32-bit integer");
        return false;
    }
    target = static_cast<std::int32_t>(*value);
    return true;
}
} // namespace

struct EditorSystem::State
{
    GameplaySession& gameplay;
    EditorEnvironment& environment;
    ScenePreview* preview = nullptr;
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
    std::unique_ptr<SceneCommands> scene_commands;
    std::unique_ptr<EditorCommands> commands;
    std::unique_ptr<EditorCommandChannel> command_channel;
    std::string channel_error;
    std::optional<std::int32_t> editor_frame_rate;

    State(GameplaySession& session, EditorEnvironment& editor_environment, ScenePreview* scene_preview)
        : gameplay(session), environment(editor_environment), preview(scene_preview)
    {
    }

    void restore_frame_rate() noexcept
    {
        if (editor_frame_rate)
        {
            if (!Engine::instance().set_target_frame_rate(*editor_frame_rate))
            {
                LogSystem::write(LogLevel::error, "Cannot restore the editor frame rate");
            }
            editor_frame_rate.reset();
        }
    }

    void restore_frame_rate_if_stopped() noexcept
    {
        const auto* status = gameplay.script_status();
        if (status == nullptr || !status->running)
        {
            restore_frame_rate();
        }
    }

    bool start_gameplay()
    {
        const auto* current = gameplay.script_status();
        if (current != nullptr && current->running)
        {
            return gameplay.start();
        }
        if (!environment.refresh_resources(gameplay))
        {
            return false;
        }
        auto& engine = Engine::instance();
        editor_frame_rate = engine.target_frame_rate();
        if (!engine.set_target_frame_rate(-1))
        {
            restore_frame_rate();
            return false;
        }
        if (!gameplay.start())
        {
            restore_frame_rate();
            return false;
        }
        return true;
    }

    bool publish(bool force = false)
    {
        const auto* script = gameplay.script_status();
        const bool running = !closed && script != nullptr && script->running;
        std::string error = last_error;
        if (error.empty())
        {
            error = gameplay.last_error();
        }
        if (error.empty() && script != nullptr)
        {
            error = script->last_error;
        }
        const bool previewing = !closed && preview != nullptr && preview->active();
        Json status = {{"session", session},
                       {"projectRoot", project_root},
                       {"projectState", closed               ? "closed"
                                        : transition.empty() ? (previewing ? "preview"
                                                               : running ? "running" : "idle")
                                                             : transition},
                       {"running", running},
                       {"targetFrameRate", Engine::instance().target_frame_rate()},
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
#if ENGINE_HAS_ASSET_PIPELINE
        // 启动锁覆盖刷新、资源取得及最终状态发布，避免与其他进程导入交错。
        asset_pipeline::ProjectLock start_lock;
#endif
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
#if ENGINE_HAS_ASSET_PIPELINE
                const auto import_lock =
                    PathUtils::from_utf8(project_root) / "cache" / ".asset-build.lock";
                if (!plain_path(import_lock) || !start_lock.acquire(import_lock, last_error))
                {
                    last_error =
                        "Resource import is active; wait for it before starting the project: " +
                        last_error;
                }
                else
                {
                    if (preview != nullptr)
                    {
                        preview->close();
                    }
                    last_success = start_gameplay();
                    if (!last_success)
                    {
                        last_error = gameplay.last_error().empty()
                                         ? "Cannot refresh the editor resource directory"
                                         : std::string(gameplay.last_error());
                    }
                }
#else
                last_error = "Project startup requires the editor resource pipeline";
#endif
            }
            else
            {
                if (preview != nullptr)
                {
                    preview->close();
                }
                gameplay.stop();
                restore_frame_rate();
                if (commands != nullptr)
                {
                    commands->clear_history();
                }
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

EditorSystem::EditorSystem(EditorEnvironment& environment, GameplaySession* gameplay,
                           WorldSystem* world, ScenePreview* preview) noexcept
    : environment_(environment), gameplay_(gameplay), world_(world), preview_(preview)
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
    std::int32_t target_frame_rate = 60;
    if (!read_frame_rate(PathUtils::from_utf8(environment_.project_identity_), target_frame_rate) ||
        !Engine::instance().set_target_frame_rate(target_frame_rate))
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
    if (gameplay_ != nullptr)
    {
        if (!App::is_initialized())
        {
            return false;
        }
        state_ = std::make_unique<State>(*gameplay_, environment_, preview_);
        state_->directory = App::persistent_data_path();
        if (!plain_path(state_->directory))
        {
            return false;
        }
        state_->project_root = environment_.project_identity_;
        state_->session =
            environment_.session_marker_.empty()
                ? std::to_string(timestamp_ms()) + "-" + std::to_string(SDL_GetPerformanceCounter())
                : PathUtils::to_utf8(environment_.session_marker_.stem());
        if (!state_->publish(true))
        {
            return false;
        }
        if (world_ != nullptr)
        {
            if (!world_->is_initialized())
            {
                return false;
            }
            state_->scene_commands = std::make_unique<SceneCommands>(*world_);
            state_->commands = std::make_unique<EditorCommands>(
                [world = world_]()
                { return world->scene() == nullptr ? 0 : world->scene()->id(); });
            if (!state_->scene_commands->register_commands(*state_->commands))
            {
                return false;
            }
            state_->command_channel = std::make_unique<EditorCommandChannel>(
                *state_->commands, state_->directory, state_->project_root, state_->session);
        }
    }
    return true;
}

bool EditorSystem::on_tick(double)
{
    if (state_ != nullptr)
    {
        state_->restore_frame_rate_if_stopped();
    }
    if (!environment_.is_initialized() || (state_ != nullptr && !state_->poll()))
    {
        return false;
    }
    if (state_ != nullptr && state_->command_channel != nullptr)
    {
        if (!state_->command_channel->poll())
        {
            const auto& error = state_->command_channel->last_error();
            if (state_->channel_error != error)
            {
                LogSystem::write(LogLevel::error, error);
                state_->channel_error = error;
            }
        }
        else
        {
            state_->channel_error.clear();
        }
        // 预览命令可能结束玩法；恢复发生在其停止回调之后。
        state_->restore_frame_rate_if_stopped();
    }
    return true;
}

SceneCommands* EditorSystem::scene_commands() noexcept
{
    return state_ ? state_->scene_commands.get() : nullptr;
}

EditorCommands* EditorSystem::commands() noexcept
{
    return is_initialized() && state_ != nullptr ? state_->commands.get() : nullptr;
}

void EditorSystem::on_shutdown() noexcept
{
    if (state_ != nullptr)
    {
        if (preview_ != nullptr)
        {
            preview_->close();
        }
        gameplay_->stop();
        state_->restore_frame_rate();
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
} // namespace engine::editor
