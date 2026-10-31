#if ENGINE_HAS_CHARACTER_CONTROLLER_EDITOR
#include "plugins/editor/character_controller/character_controller_codec.hpp"
#endif
#if ENGINE_HAS_CHARACTER_CONTROLLER_EDITOR
#include "plugins/editor/character_controller/character_controller_commands.hpp"
#endif
#include "editor/editor_environment.hpp"
#include "editor/editor_system.hpp"
#include "editor/project/project_system.hpp"
#include "editor/render/render_commands.hpp"
#include "editor/scene/scene_preview.hpp"
#include "runtime/app/app.hpp"
#include "runtime/app/runtime_host.hpp"
#include "runtime/engine.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"
#include "runtime_plugins.hpp"

#if ENGINE_HAS_FONT_IMPORTER
#include "plugins/editor/font_import/font_importer.hpp"
#endif
#if ENGINE_HAS_UI_IMPORTER
#include "plugins/editor/ui_import/ui_importer.hpp"
#endif
#if ENGINE_HAS_FBX_IMPORTER
#include "plugins/editor/fbx_import/fbx_importer.hpp"
#endif
#if ENGINE_HAS_USD_IMPORTER
#include "plugins/editor/usd_import/usd_importer.hpp"
#endif

#if ENGINE_HAS_MODELING_IMPORTER
#include "plugins/editor/modeling/modeling_mesh_importer.hpp"
#endif

#if ENGINE_HAS_MODELING_PLUGIN
#include "plugins/editor/modeling/modeling_commands.hpp"
#endif

#if ENGINE_HAS_SCENE_BAKE
#include "editor/commands/editor_commands.hpp"
#include "plugins/editor/scene_bake/scene_bake_system.hpp"
#include "runtime/scene/scene.hpp"
#include <nlohmann/json.hpp>
#endif

#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
#include "plugins/runtime/resource_graph/resource_graph_system.hpp"
#include "runtime/resource/resource_system.hpp"
#endif

#if ENGINE_HAS_ASSET_PIPELINE
#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "editor/scene/scene_persistence.hpp"
#endif

#if ENGINE_HAS_SCRIPT_PLUGIN
#include "plugins/runtime/script/script_system.hpp"
#endif

#include <SDL3/SDL_main.h>
#include <fmt/base.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

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

int run_editor(const fs::path& project_root, engine::editor::EditorEnvironment& environment,
               const engine::EngineConfig& config, std::uint64_t frame_limit)
{
#if ENGINE_HAS_ASSET_PIPELINE
    engine::editor::AssetPipelineSystem pipeline;
#endif
#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
    engine::ResourceSystem resources;
    engine::plugins::resource_graph::ResourceGraphSystem resource_graph(resources);
#endif
    engine::RuntimeHost runtime;
    engine::ScriptRuntimeFactory script_factory = nullptr;
#if ENGINE_HAS_SCRIPT_PLUGIN
    script_factory = [](engine::WorldSystem& world, engine::RenderSystem& renderer,
                        engine::UiSystem& ui) -> std::unique_ptr<engine::ScriptRuntime>
    { return std::make_unique<engine::plugins::script::ScriptSystem>(&world, &renderer, &ui); };
#endif
#if ENGINE_HAS_MODELING_PLUGIN
    std::unique_ptr<engine::plugins::modeling::ModelingCommands> modeling;
#endif
    std::unique_ptr<engine::editor::RenderCommands> render_commands;
    std::unique_ptr<engine::editor::ScenePreview> preview;
#if ENGINE_HAS_SCENE_BAKE
    std::unique_ptr<engine::plugins::scene_bake::SceneBakeSystem> scene_bake;
#endif
#if ENGINE_HAS_ASSET_PIPELINE
    std::unique_ptr<engine::editor::ScenePersistence> persistence;
#endif
    std::unique_ptr<engine::editor::EditorSystem> editor;
    std::uint64_t frames = 0;
    const int result = [&]()
    {
#if ENGINE_HAS_ASSET_PIPELINE
        if (!pipeline.initialize(config))
        {
            LogSystem::write(LogLevel::error, "Asset pipeline initialization failed");
            return 1;
        }
#if ENGINE_HAS_CHARACTER_CONTROLLER_EDITOR
        if (!engine::plugins::character_controller::register_controller_codec())
        {
            std::fputs("Cannot register character controller scene codec\n", stderr);
            return 1;
        }
#endif
#if ENGINE_HAS_FONT_IMPORTER
        std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> font_importer =
            std::make_unique<engine::plugins::font_import::FontImporter>();
        if (!pipeline.add_importer(font_importer))
        {
            std::fputs("Cannot register font importer\n", stderr);
            return 1;
        }
#endif
#if ENGINE_HAS_UI_IMPORTER
        std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> ui_importer =
            std::make_unique<engine::plugins::ui_import::UiImporter>();
        if (!pipeline.add_importer(ui_importer))
        {
            std::fputs("Cannot register ui importer\n", stderr);
            return 1;
        }
#endif
#if ENGINE_HAS_FBX_IMPORTER
        std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> fbx_importer =
            std::make_unique<engine::plugins::fbx_import::FbxImporter>();
        if (!pipeline.add_importer(fbx_importer))
        {
            std::fputs("Cannot register FBX importer\n", stderr);
            return 1;
        }
#endif
#if ENGINE_HAS_USD_IMPORTER
        std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> usd_importer =
            std::make_unique<engine::plugins::usd_import::UsdImporter>();
        if (!pipeline.add_importer(usd_importer))
        {
            std::fputs("Cannot register USD importer\n", stderr);
            return 1;
        }
#endif
#if ENGINE_HAS_MODELING_IMPORTER
        std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> importer =
            std::make_unique<engine::plugins::modeling::ModelingMeshImporter>();
        if (!pipeline.set_importer(importer))
        {
            LogSystem::write(LogLevel::error, "Cannot register modeling importer");
            return 1;
        }
#endif
        if (!pipeline.build_assets(project_root))
        {
            LogSystem::write(LogLevel::error, "Asset build failed: " + pipeline.last_error());
            return 1;
        }
#endif
#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
        if (!resources.initialize(config) || !resource_graph.initialize(config))
        {
            LogSystem::write(LogLevel::error, "Resource services initialization failed");
            return 1;
        }
        resources.set_loader(&resource_graph);
        const bool runtime_ready =
            runtime.initialize(config, &resources, script_factory, configure_runtime_plugins);
#else
        const bool runtime_ready =
            runtime.initialize(config, nullptr, script_factory, configure_runtime_plugins);
#endif
        if (!runtime_ready)
        {
            LogSystem::write(LogLevel::error, "Engine initialization failed");
            return 1;
        }
#if ENGINE_HAS_ASSET_PIPELINE
        if (!environment.apply_resources(pipeline.catalog(), *runtime.gameplay()))
        {
            LogSystem::write(LogLevel::error, "Cannot submit editor resource directory: " +
                                                  std::string(runtime.gameplay()->last_error()));
            return 1;
        }
#endif
        engine::editor::ScenePreview::ImportCallback import;
#if ENGINE_HAS_ASSET_PIPELINE
        import = [&pipeline, &project_root](std::string& error)
        {
            if (!pipeline.build_assets(project_root))
            {
                error = pipeline.last_error();
                return false;
            }
            return true;
        };
#endif
        preview = std::make_unique<engine::editor::ScenePreview>(
            environment, *runtime.gameplay(), *runtime.world(), *runtime.renderer(),
            *runtime.window(), project_root, std::move(import));
        if (!preview->initialize(config))
        {
            LogSystem::write(LogLevel::error, preview->last_error());
            return 1;
        }
        editor = std::make_unique<engine::editor::EditorSystem>(environment, runtime.gameplay(),
                                                                runtime.world(), preview.get());
        if (!editor->initialize(config))
        {
            LogSystem::write(LogLevel::error, "Editor initialization failed");
            return 1;
        }
        if (!editor->commands() || !preview->register_commands(*editor->commands()))
        {
            LogSystem::write(LogLevel::error, "Cannot register scene preview commands");
            return 1;
        }
#if ENGINE_HAS_CHARACTER_CONTROLLER_EDITOR
        if (!editor->scene_commands() || !editor->commands() ||
            !engine::plugins::character_controller::register_controller_commands(
                *runtime.world(), *editor->scene_commands(), *editor->commands()))
        {
            LogSystem::write(LogLevel::error, "Cannot register controller commands");
            return 1;
        }
#endif

#if ENGINE_HAS_MODELING_PLUGIN
        if (editor->commands() != nullptr && runtime.world() != nullptr)
        {
            modeling = std::make_unique<engine::plugins::modeling::ModelingCommands>(
                *runtime.world(), project_root);
            if (!modeling->register_commands(*editor->commands()))
            {
                LogSystem::write(LogLevel::error, "Cannot register modeling commands");
                return 1;
            }
        }
#endif
        if (editor->commands() != nullptr && runtime.renderer() != nullptr)
        {
            render_commands = std::make_unique<engine::editor::RenderCommands>(
                *runtime.renderer(), *runtime.world(), project_root / "cache");
            if (!render_commands->register_commands(*editor->commands()))
            {
                LogSystem::write(LogLevel::error, "Cannot register render commands");
                return 1;
            }
        }
#if ENGINE_HAS_ASSET_PIPELINE
        if (editor->commands() != nullptr && runtime.world() != nullptr)
        {
            engine::editor::ScenePersistence::MeshSourceWriter mesh_writer;
#if ENGINE_HAS_MODELING_PLUGIN
            if (modeling != nullptr)
            {
                mesh_writer = [&modeling](const engine::Node& node, const fs::path& destination,
                                          std::string& error)
                { return modeling->export_mesh(node, destination, error); };
            }
#endif
            persistence = std::make_unique<engine::editor::ScenePersistence>(
                *runtime.world(), project_root, std::move(mesh_writer),
                [&preview](std::string_view path,
                           std::span<const engine::editor::SceneSavedResource> changed)
                { preview->mark_saved(path, changed); });
            if (!persistence->register_commands(*editor->commands()))
            {
                LogSystem::write(LogLevel::error, "Cannot register scene persistence commands");
                return 1;
            }
        }
#endif
#if ENGINE_HAS_SCENE_BAKE
        scene_bake = std::make_unique<engine::plugins::scene_bake::SceneBakeSystem>(
            resources, *runtime.renderer(), project_root,
            [&editor, &preview, &runtime](std::string_view path, std::uint64_t& scene_id,
                                          std::string& error)
            {
                scene_id = 0;
                nlohmann::json status;
                if (!preview->active() || runtime.world()->scene() == nullptr ||
                    !editor->commands()->execute("get_scene_preview", nlohmann::json::object(),
                                                 status, error))
                {
                    error = "Baking requires an active saved scene preview";
                    return false;
                }
                if (status.value("path", std::string{}) != path || status.value("modified", true) ||
                    status.value("reloadPending", true))
                {
                    error = "Save or refresh the matching scene preview before baking";
                    return false;
                }
                scene_id = runtime.world()->scene()->id();
                return true;
            },
            [&editor](std::string& error)
            {
                nlohmann::json result;
                return editor->commands()->execute("refresh_scene_preview",
                                                   {{"discardChanges", false}}, result, error);
            });
        if (!scene_bake->initialize(config) || !scene_bake->register_commands(*editor->commands()))
        {
            LogSystem::write(LogLevel::error, "Cannot initialize scene baking plugin");
            return 1;
        }
#endif
        LogSystem::write(LogLevel::info, "Editor ready; waiting for MCP start_project");
        auto& frame_engine = engine::Engine::instance();
        auto previous_time = std::chrono::steady_clock::now();
        for (;;)
        {
            frame_engine.begin_frame();
            const auto current_time = std::chrono::steady_clock::now();
            const double delta_time =
                std::min(std::chrono::duration<double>(current_time - previous_time).count(), 0.1);
            previous_time = current_time;
            if (!editor->tick(delta_time))
            {
                LogSystem::write(LogLevel::error, "Editor control frame failed");
                return 1;
            }
#if ENGINE_HAS_SCENE_BAKE
            if (!scene_bake->tick(delta_time))
            {
                LogSystem::write(LogLevel::error, "Scene baking plugin frame failed");
                return 1;
            }
#endif
#if ENGINE_HAS_MODELING_PLUGIN
            if (modeling != nullptr)
            {
                modeling->synchronize_scene();
            }
#endif
            auto status = engine::FrameStatus::running;
            if (preview->active())
            {
                auto& window = *runtime.window();
                if (!window.tick(delta_time))
                {
                    status = engine::FrameStatus::failed;
                }
                else if (window.exit_requested())
                {
                    status = engine::FrameStatus::exit_requested;
                }
                else if (!preview->tick(delta_time))
                {
                    LogSystem::write(LogLevel::error, preview->last_error());
                    status = engine::FrameStatus::failed;
                }
                else if (!window.can_render())
                {
                    status = engine::FrameStatus::paused;
                }
                else if (!runtime.renderer()->render(preview->camera()))
                {
                    LogSystem::write(LogLevel::error, "Scene preview rendering failed");
                    preview->close();
                    if (!runtime.renderer()->render())
                    {
                        status = engine::FrameStatus::failed;
                    }
                }
            }
            else
            {
                status = runtime.tick(delta_time);
            }
#if ENGINE_HAS_MODELING_PLUGIN
            if (modeling != nullptr)
            {
                modeling->synchronize_scene();
            }
#endif
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
            frame_engine.end_frame();
            ++frames;
            if (frame_limit != 0 && frames >= frame_limit)
            {
                break;
            }
        }
        return 0;
    }();

    // 停止烘焙提交；在途 GPU 读回内存保留至渲染器彻底关闭。
#if ENGINE_HAS_SCENE_BAKE
    if (scene_bake != nullptr)
    {
        scene_bake->shutdown();
    }
#endif
    // 玩法回调执行时编辑器和全部引擎系统仍就绪。
    if (preview != nullptr)
    {
        preview->close();
    }
    if (editor != nullptr)
    {
        editor->shutdown();
        editor.reset();
    }
#if ENGINE_HAS_ASSET_PIPELINE
    persistence.reset();
#endif
    render_commands.reset();
    preview.reset();
#if ENGINE_HAS_MODELING_PLUGIN
    modeling.reset();
#endif
    runtime.shutdown();
#if ENGINE_HAS_SCENE_BAKE
    scene_bake.reset();
#endif
#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
    resources.set_loader(nullptr);
    resource_graph.shutdown();
    resources.shutdown();
#endif
#if ENGINE_HAS_ASSET_PIPELINE
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
    engine::editor::ProjectSystem project(PathUtils::to_utf8(project_root));
    if (!project.initialize(config))
    {
        LogSystem::write(LogLevel::error, "Editor project initialization failed");
        return 1;
    }
    engine::editor::EditorEnvironment environment(project, runtime_directory);
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
