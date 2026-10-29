#include "app.hpp"
#include "editor_system.hpp"
#include "engine.hpp"
#include "engine_config.hpp"
#include "log_system.hpp"
#include "project_system.hpp"

#if ENGINE_HAS_SCRIPT_RUNTIME
#include "asset_pipeline_system.hpp"
#include "asset_sync_system.hpp"
#include "script_system.hpp"
#endif

#include <SDL3/SDL_main.h>
#include <fmt/base.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>

namespace
{
namespace fs = std::filesystem;
using engine::LogLevel;
using engine::LogSystem;

fs::path utf8_path(std::string_view text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string path_text(const fs::path& path)
{
    const auto text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

void print_usage()
{
    fmt::print("Usage: editor_app --project <project-directory> "
               "[--runtime-dir <persistent-directory>] [--frames <positive-count>]\n");
}

std::string project_name(const fs::path& path)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char value : path_text(path))
    {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string suffix(16, '0');
    for (std::size_t index = suffix.size(); index != 0; --index)
    {
        suffix[index - 1] = digits[hash & 15U];
        hash >>= 4U;
    }
    return "engine-editor-" + suffix;
}

#if ENGINE_HAS_SCRIPT_RUNTIME
bool prepare_builtin(const fs::path& source, const fs::path& target)
{
    std::error_code error;
    fs::create_directories(target.parent_path(), error);
    if (error)
    {
        return false;
    }
    // 每会话独占快照；后续 TS 构建和 MCP 同步都不能改写此路径。
    fs::copy(source, target, fs::copy_options::recursive, error);
    return !error;
}
#endif

int run_editor(const fs::path& project_root, const engine::EngineConfig& config,
               std::uint64_t frame_limit)
{
    engine::ProjectSystem project(path_text(project_root));
#if ENGINE_HAS_SCRIPT_RUNTIME
    engine::AssetPipelineSystem pipeline;
    engine::AssetSyncSystem sync;
#endif
    auto& runtime = engine::Engine::instance();
    engine::EditorSystem editor(project);
    std::uint64_t frames = 0;
    const int result = [&]()
    {
        if (!project.initialize(config))
        {
            LogSystem::write(LogLevel::error, "Editor project initialization failed");
            return 1;
        }
#if ENGINE_HAS_SCRIPT_RUNTIME
        if (!pipeline.initialize(config) || !pipeline.build_scripts(project_root))
        {
            LogSystem::write(LogLevel::error, "Script build failed: " + pipeline.last_error());
            return 1;
        }
        if (!prepare_builtin(project_root / "cache" / "assets", engine::App::builtin_assets_path()))
        {
            LogSystem::write(LogLevel::error, "Cannot prepare immutable editor built-in assets");
            return 1;
        }
        if (!sync.initialize(config) ||
            !sync.sync(project_root / "cache" / "assets", engine::App::persistent_data_path()))
        {
            LogSystem::write(LogLevel::error, "Initial asset sync failed: " + sync.last_error());
            return 1;
        }
#endif
        if (!runtime.initialize(config))
        {
            LogSystem::write(LogLevel::error, "Engine initialization failed");
            return 1;
        }
        if (!editor.initialize(config))
        {
            LogSystem::write(LogLevel::error, "Editor initialization failed");
            return 1;
        }
#if ENGINE_HAS_SCRIPT_RUNTIME
        if (!runtime.start_gameplay())
        {
            const auto* status = runtime.script_status();
            const std::string detail =
                status != nullptr ? status->last_error : "script system unavailable";
            LogSystem::write(LogLevel::error, "Gameplay startup failed: " + detail);
            return 1;
        }
        LogSystem::write(LogLevel::info, "Gameplay entry started: scripts/game.js");
#endif
        auto previous_time = std::chrono::steady_clock::now();
        for (;;)
        {
            const auto current_time = std::chrono::steady_clock::now();
            const double delta_time =
                std::min(std::chrono::duration<double>(current_time - previous_time).count(), 0.1);
            previous_time = current_time;
            const auto status = runtime.tick(delta_time);
            if (status == engine::FrameStatus::exit_requested)
            {
                break;
            }
            if (status == engine::FrameStatus::failed)
            {
                LogSystem::write(LogLevel::error, "Engine frame failed");
                return 1;
            }
            if (status == engine::FrameStatus::paused)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                previous_time = std::chrono::steady_clock::now();
                continue;
            }
            if (!editor.tick(delta_time))
            {
                LogSystem::write(LogLevel::error, "Editor frame failed");
                return 1;
            }
            ++frames;
            if (frame_limit != 0 && frames >= frame_limit)
            {
                runtime.request_exit();
            }
        }
        return 0;
    }();

    // 玩法回调执行时编辑器和全部引擎系统仍就绪。
    runtime.stop_gameplay();
    editor.shutdown();
    runtime.shutdown();
#if ENGINE_HAS_SCRIPT_RUNTIME
    sync.shutdown();
    pipeline.shutdown();
#endif
    project.shutdown();
    LogSystem::write(LogLevel::info,
                     "Editor stopped after " + std::to_string(frames) + " rendered frames");
    return result;
}

} // namespace

int main(int argc, char* argv[])
{
    engine::EngineConfig config;
    config.window_title = "Engine Editor";
    std::string project_argument;
    fs::path runtime_directory;
    std::uint64_t frame_limit = 0;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
        if (argument == "--help" && argc == 2)
        {
            print_usage();
            return 0;
        }
        if (argument == "--project" && index + 1 < argc && project_argument.empty())
        {
            project_argument = argv[++index];
            if (project_argument.empty())
            {
                print_usage();
                return 2;
            }
        }
        else if (argument == "--runtime-dir" && index + 1 < argc && runtime_directory.empty())
        {
            runtime_directory = utf8_path(argv[++index]);
            if (runtime_directory.empty())
            {
                print_usage();
                return 2;
            }
        }
        else if (argument == "--frames" && index + 1 < argc && frame_limit == 0)
        {
            const std::string_view value(argv[++index]);
            const auto result =
                std::from_chars(value.data(), value.data() + value.size(), frame_limit);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
                frame_limit == 0)
            {
                print_usage();
                return 2;
            }
        }
        else
        {
            print_usage();
            return 2;
        }
    }
    if (project_argument.empty())
    {
        print_usage();
        return 2;
    }
    std::error_code error;
    const auto absolute_project = fs::absolute(utf8_path(project_argument), error);
    if (error)
    {
        LogSystem::write(LogLevel::error, "Cannot resolve editor project path");
        return 1;
    }
    const auto project_root = fs::weakly_canonical(absolute_project, error);
    if (error)
    {
        LogSystem::write(LogLevel::error, "Cannot normalize editor project path");
        return 1;
    }
    engine::AppConfig app_config;
    app_config.application_name = project_name(project_root);
    app_config.persistent_data_path = runtime_directory;
#if ENGINE_HAS_SCRIPT_RUNTIME
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto session_directory =
        project_root / "cache" / "editor-builtin" / std::to_string(nonce);
    app_config.builtin_assets_path = session_directory / "assets";
#else
    app_config.builtin_assets_path = project_root / "assets";
#endif
    if (!engine::App::initialize(app_config))
    {
        LogSystem::write(LogLevel::error, "Cannot initialize application paths");
        return 1;
    }
    auto& log = LogSystem::instance();
    if (!log.initialize(config))
    {
        LogSystem::write(LogLevel::error, "Cannot initialize disk logging");
        engine::App::shutdown();
        return 1;
    }
    LogSystem::write(LogLevel::info, "Editor project: " + path_text(project_root));
    LogSystem::write(LogLevel::info, "Editor persistent directory: " +
                                         path_text(engine::App::persistent_data_path()));
    LogSystem::write(LogLevel::info, "Editor log: " + path_text(log.file_path()));
    const int result = run_editor(project_root, config, frame_limit);
#if ENGINE_HAS_SCRIPT_RUNTIME
    fs::remove_all(session_directory, error);
    if (error)
    {
        LogSystem::write(LogLevel::warning,
                         "Cannot remove editor built-in snapshot: " + error.message());
    }
#endif
    const bool flushed = log.flush();
    log.shutdown();
    engine::App::shutdown();
    return flushed ? result : 1;
}
