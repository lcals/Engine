#include "editor/editor_environment.hpp"
#include "editor/editor_system.hpp"
#include "editor/modeling/modeling_commands.hpp"
#include "editor/project/project_system.hpp"
#include "editor/render/render_commands.hpp"
#include "runtime/app/app.hpp"
#include "runtime/app/runtime_host.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/utils/path_utils.hpp"

#if ENGINE_HAS_SCRIPT_RUNTIME
#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "editor/scene/scene_persistence.hpp"
#include "runtime/script/script_system.hpp"
#endif

#include <SDL3/SDL_main.h>
#include <fmt/base.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace
{
namespace fs = std::filesystem;
using engine::LogLevel;
using engine::LogSystem;
using engine::PathUtils;

void print_usage()
{
    fmt::print("Usage: editor_app --project <project-directory> "
               "[--runtime-dir <persistent-directory>] [--frames <positive-count>] [--width "
               "<pixels>] [--height <pixels>]\n");
}

int run_editor(const fs::path& project_root, engine::EditorEnvironment& environment,
               const engine::EngineConfig& config, std::uint64_t frame_limit)
{
#if ENGINE_HAS_SCRIPT_RUNTIME
    engine::AssetPipelineSystem pipeline;
#endif
    engine::RuntimeHost runtime;
    std::unique_ptr<engine::ModelingCommands> modeling;
    std::unique_ptr<engine::RenderCommands> render_commands;
#if ENGINE_HAS_SCRIPT_RUNTIME
    std::unique_ptr<engine::ScenePersistence> persistence;
#endif
    std::unique_ptr<engine::EditorSystem> editor;
    std::uint64_t frames = 0;
    const int result = [&]()
    {
#if ENGINE_HAS_SCRIPT_RUNTIME
        if (!pipeline.initialize(config) || !pipeline.build_assets(project_root))
        {
            LogSystem::write(LogLevel::error, "Asset build failed: " + pipeline.last_error());
            return 1;
        }
#endif
        if (!runtime.initialize(config))
        {
            LogSystem::write(LogLevel::error, "Engine initialization failed");
            return 1;
        }
#if ENGINE_HAS_SCRIPT_RUNTIME
        if (!environment.apply_resources(pipeline.catalog(), *runtime.gameplay()))
        {
            LogSystem::write(LogLevel::error, "Cannot submit editor resource directory: " +
                                                  std::string(runtime.gameplay()->last_error()));
            return 1;
        }
#endif
        editor = std::make_unique<engine::EditorSystem>(environment, runtime.gameplay(),
                                                        runtime.world());
        if (!editor->initialize(config))
        {
            LogSystem::write(LogLevel::error, "Editor initialization failed");
            return 1;
        }
        if (editor->commands() != nullptr && runtime.world() != nullptr)
        {
            modeling = std::make_unique<engine::ModelingCommands>(*runtime.world(), project_root);
            if (!modeling->register_commands(*editor->commands()))
            {
                LogSystem::write(LogLevel::error, "Cannot register modeling commands");
                return 1;
            }
        }
        if (editor->commands() != nullptr && runtime.renderer() != nullptr)
        {
            render_commands = std::make_unique<engine::RenderCommands>(*runtime.renderer(),
                                                                       project_root / "cache");
            if (!render_commands->register_commands(*editor->commands()))
            {
                LogSystem::write(LogLevel::error, "Cannot register render commands");
                return 1;
            }
        }
#if ENGINE_HAS_SCRIPT_RUNTIME
        if (modeling != nullptr)
        {
            persistence = std::make_unique<engine::ScenePersistence>(
                *runtime.world(), project_root,
                [&modeling](const engine::Node& node, const fs::path& destination,
                            std::string& error)
                { return modeling->export_mesh(node, destination, error); });
            if (!persistence->register_commands(*editor->commands()))
            {
                LogSystem::write(LogLevel::error, "Cannot register scene persistence commands");
                return 1;
            }
        }
#endif
        LogSystem::write(LogLevel::info, "Editor ready; waiting for MCP start_project");
        auto previous_time = std::chrono::steady_clock::now();
        for (;;)
        {
            const auto current_time = std::chrono::steady_clock::now();
            const double delta_time =
                std::min(std::chrono::duration<double>(current_time - previous_time).count(), 0.1);
            previous_time = current_time;
            if (!editor->tick(delta_time))
            {
                LogSystem::write(LogLevel::error, "Editor control frame failed");
                return 1;
            }
            if (modeling != nullptr)
            {
                modeling->synchronize_scene();
            }
            const auto status = runtime.tick(delta_time);
            if (modeling != nullptr)
            {
                modeling->synchronize_scene();
            }
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
            ++frames;
            if (frame_limit != 0 && frames >= frame_limit)
            {
                runtime.request_exit();
            }
        }
        return 0;
    }();

    // 玩法回调执行时编辑器和全部引擎系统仍就绪。
    if (editor != nullptr)
    {
        editor->shutdown();
        editor.reset();
    }
#if ENGINE_HAS_SCRIPT_RUNTIME
    persistence.reset();
#endif
    render_commands.reset();
    modeling.reset();
    runtime.shutdown();
#if ENGINE_HAS_SCRIPT_RUNTIME
    pipeline.shutdown();
#endif
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
        if ((argument == "--width" || argument == "--height") && index + 1 < argc)
        {
            const std::string_view value(argv[++index]);
            std::uint32_t pixels = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), pixels);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                pixels < 320 || pixels > 8192)
            {
                fmt::print(stderr, "Window dimension must be within 320..8192 pixels\n");
                return 2;
            }
            (argument == "--width" ? config.width : config.height) = pixels;
            continue;
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
            runtime_directory = PathUtils::from_utf8(argv[++index]);
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
    const auto absolute_project = fs::absolute(PathUtils::from_utf8(project_argument), error);
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
    engine::ProjectSystem project(PathUtils::to_utf8(project_root));
    if (!project.initialize(config))
    {
        LogSystem::write(LogLevel::error, "Editor project initialization failed");
        return 1;
    }
    engine::EditorEnvironment environment(project, runtime_directory);
    if (!environment.initialize())
    {
        LogSystem::write(LogLevel::error, "Cannot initialize editor environment");
        return 1;
    }
    auto& log = LogSystem::instance();
    if (!log.initialize(config))
    {
        LogSystem::write(LogLevel::error, "Cannot initialize disk logging");
        environment.shutdown();
        return 1;
    }
    LogSystem::write(LogLevel::info, "Editor project: " + PathUtils::to_utf8(project_root));
    LogSystem::write(LogLevel::info, "Editor persistent directory: " +
                                         PathUtils::to_utf8(engine::App::persistent_data_path()));
    LogSystem::write(LogLevel::info, "Editor log: " + PathUtils::to_utf8(log.file_path()));
    const int result = run_editor(project_root, environment, config, frame_limit);
    const bool flushed = log.flush();
    log.shutdown();
    environment.shutdown();
    project.shutdown();
    return flushed ? result : 1;
}
