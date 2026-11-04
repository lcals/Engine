#include "runtime/performance/profiling.hpp"
#if ENGINE_HAS_TERRAIN_EDITOR
#include "plugins/editor/terrain/terrain_codec.hpp"
#include "plugins/editor/terrain/terrain_importer.hpp"
#endif
#if ENGINE_HAS_DESTRUCTION_EDITOR
#include "plugins/editor/destruction/destruction_codec.hpp"
#include "plugins/editor/destruction/destruction_importer.hpp"
#endif
#if ENGINE_HAS_TERRAIN_EDITOR
#include "plugins/editor/terrain/terrain_commands.hpp"
#endif
#if ENGINE_HAS_DESTRUCTION_EDITOR
#include "plugins/editor/destruction/destruction_commands.hpp"
#endif
#if ENGINE_HAS_EFFECTS_EDITOR
#include "plugins/editor/effects/effect_codec.hpp"
#include "plugins/editor/effects/effect_importer.hpp"
#endif
#if ENGINE_HAS_EFFECTS_EDITOR
#include "plugins/editor/effects/effect_commands.hpp"
#endif

#if ENGINE_HAS_CHARACTER_CONTROLLER_EDITOR
#include "plugins/editor/character_controller/character_controller_codec.hpp"
#endif
#if ENGINE_HAS_NAVIGATION_CROWD_EDITOR
#include "plugins/editor/navigation_crowd/navigation_agent_codec.hpp"
#endif
#if ENGINE_HAS_CHARACTER_CONTROLLER_EDITOR
#include "plugins/editor/character_controller/character_controller_commands.hpp"
#endif
#include "editor/commands/editor_commands.hpp"
#if ENGINE_HAS_ASSET_PIPELINE
#include "editor/asset_pipeline/mesh_lod_commands.hpp"
#endif
#include "editor/editor_environment.hpp"
#include "editor/editor_system.hpp"
#include "editor/project/project_system.hpp"
#include "editor/project/runtime_settings_io.hpp"
#include "editor/render/render_commands.hpp"
#include "editor/scene/scene_preview.hpp"
#include "runtime/app/app.hpp"
#include "runtime/app/runtime_host.hpp"
#include "runtime/engine.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"
#include "runtime_plugins.hpp"

#if ENGINE_HAS_TERRAIN_PLUGIN
#include "plugins/runtime/terrain/terrain_component.hpp"
#include "runtime/scene/scene.hpp"
#endif
#if ENGINE_HAS_DESTRUCTION_PLUGIN
#include "plugins/runtime/destruction/destruction_component.hpp"
#include "runtime/scene/scene.hpp"
#endif

#if ENGINE_HAS_FONT_IMPORTER
#include "plugins/editor/font_import/font_importer.hpp"
#endif
#if ENGINE_HAS_UI_IMPORTER
#include "plugins/editor/ui_import/ui_importer.hpp"
#endif
#if ENGINE_HAS_JSON_IMPORTER
#include "plugins/editor/json_import/json_importer.hpp"
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
#include "plugins/editor/scene_bake/scene_bake_system.hpp"
#include "runtime/scene/scene.hpp"
#endif
#if ENGINE_HAS_UI_EDITOR
#include "plugins/editor/ui/ui_editor_commands.hpp"
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
#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
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
               "<pixels>] [--height <pixels>] [--workers <0..64>] [--runtime-profile "
               "<desktop|mobile>]\n");
}

int run_editor(const fs::path& project_root, engine::editor::EditorEnvironment& environment,
               const engine::EngineConfig& config, std::uint64_t frame_limit,
               const engine::RuntimeParallelConfig& parallel)
{
    engine::ThreadSystem threads(parallel.threads);
#if ENGINE_HAS_ASSET_PIPELINE
    engine::TaskSystem tasks(threads, {"Editor", 8, engine::TaskPriority::low});
    engine::editor::AssetPipelineSystem pipeline(tasks);
#endif
#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
    engine::ResourceSystem resources;
    engine::plugins::resource_graph::ResourceGraphSystem resource_graph(resources);
#endif
    engine::RuntimeHost runtime;
    engine::ScriptRuntimeFactory script_factory = nullptr;
#if ENGINE_HAS_SCRIPT_PLUGIN
    script_factory = [](engine::WorldSystem& world, engine::RenderSystem& renderer,
                        engine::UiSystem& ui, engine::PhysicsSystem& physics,
                        engine::NavigationSystem& navigation, engine::InputSystem& input,
                        engine::WindowSystem& window) -> std::unique_ptr<engine::ScriptRuntime>
    {
        return std::make_unique<engine::plugins::script::ScriptSystem>(
            &world, &renderer, &ui, &physics, &navigation, &input, &window);
    };
#endif
#if ENGINE_HAS_MODELING_PLUGIN
    std::unique_ptr<engine::plugins::modeling::ModelingCommands> modeling;
#endif
    std::unique_ptr<engine::editor::RenderCommands> render_commands;
    std::unique_ptr<engine::editor::ScenePreview> preview;
#if ENGINE_HAS_UI_EDITOR
    std::unique_ptr<engine::plugins::ui::UiEditorCommands> ui_editor;
    std::uint64_t ui_run_id = 0;
#endif
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
        if (!threads.initialize(config))
        {
            LogSystem::write(LogLevel::error,
                             "Thread initialization failed: " + threads.last_error());
            return 1;
        }
#if ENGINE_HAS_ASSET_PIPELINE
        if (!tasks.initialize(config))
        {
            LogSystem::write(LogLevel::error,
                             "Editor tasks initialization failed: " + tasks.last_error());
            return 1;
        }
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
#if ENGINE_HAS_NAVIGATION_CROWD_EDITOR
        if (!engine::plugins::navigation_crowd::register_navigation_agent_codec())
        {
            std::fputs("Cannot register navigation agent scene codec\n", stderr);
            return 1;
        }
#endif
#if ENGINE_HAS_EFFECTS_EDITOR
        std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> effect_importer =
            std::make_unique<engine::plugins::effects::EffectImporter>();
        if (!engine::plugins::effects::register_effect_codec() ||
            !pipeline.add_importer(effect_importer))
        {
            std::fputs("Cannot register effects importer and scene codec\n", stderr);
            return 1;
        }
#endif
#if ENGINE_HAS_TERRAIN_EDITOR
        std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> terrain_importer =
            std::make_unique<engine::plugins::terrain::TerrainImporter>();
        if (!engine::plugins::terrain::register_terrain_codec() ||
            !pipeline.add_importer(terrain_importer))
        {
            std::fputs("Cannot register terrain importer and scene codec\n", stderr);
            return 1;
        }
#endif
#if ENGINE_HAS_DESTRUCTION_EDITOR
        std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> destruction_importer =
            std::make_unique<engine::plugins::destruction::DestructionImporter>();
        if (!engine::plugins::destruction::register_destruction_codec() ||
            !pipeline.add_importer(destruction_importer))
        {
            std::fputs("Cannot register destruction importer and scene codec\n", stderr);
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
#if ENGINE_HAS_JSON_IMPORTER
        std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> json_importer =
            std::make_unique<engine::plugins::json_import::JsonImporter>();
        if (!pipeline.add_importer(json_importer))
        {
            std::fputs("Cannot register JSON importer\n", stderr);
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
        const bool runtime_ready = runtime.initialize(
            config, &resources, script_factory, configure_runtime_plugins, &threads, parallel);
#else
        const bool runtime_ready = runtime.initialize(
            config, nullptr, script_factory, configure_runtime_plugins, &threads, parallel);
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
            *runtime.input(), project_root, std::move(import));
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
#if ENGINE_HAS_UI_EDITOR
        ui_editor = std::make_unique<engine::plugins::ui::UiEditorCommands>(
            *runtime.world(), *runtime.ui(), *runtime.window(), *preview, tasks, project_root);
        if (!ui_editor->register_commands(*editor->commands(), *editor->scene_commands()))
        {
            LogSystem::write(LogLevel::error, "Cannot register UI editor commands");
            return 1;
        }
        runtime.set_event_observer(
            [](void* context, const SDL_Event& event)
            { static_cast<engine::plugins::ui::UiEditorCommands*>(context)->observe_event(event); },
            ui_editor.get());
#endif
#if ENGINE_HAS_CHARACTER_CONTROLLER_EDITOR
        if (!editor->scene_commands() || !editor->commands() ||
            !engine::plugins::character_controller::register_controller_commands(
                *runtime.world(), *editor->scene_commands(), *editor->commands()))
        {
            LogSystem::write(LogLevel::error, "Cannot register controller commands");
            return 1;
        }
#endif

#if ENGINE_HAS_EFFECTS_EDITOR
        if (!editor->scene_commands() || !editor->commands() ||
            !engine::plugins::effects::register_effect_commands(
                *runtime.world(), *editor->scene_commands(), *editor->commands()))
        {
            LogSystem::write(LogLevel::error, "Cannot register effects commands");
            return 1;
        }
#endif
#if ENGINE_HAS_TERRAIN_EDITOR
        if (!editor->scene_commands() || !editor->commands() ||
            !engine::plugins::terrain::register_terrain_commands(
                *runtime.world(), *editor->scene_commands(), *editor->commands(), project_root))
        {
            LogSystem::write(LogLevel::error, "Cannot register terrain commands");
            return 1;
        }
#endif
#if ENGINE_HAS_DESTRUCTION_EDITOR
        if (!editor->scene_commands() || !editor->commands() ||
            !engine::plugins::destruction::register_destruction_commands(
                *runtime.world(), *editor->scene_commands(), *editor->commands(), project_root))
        {
            LogSystem::write(LogLevel::error, "Cannot register destruction commands");
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
            if (!render_commands->register_commands(*editor->commands(), *editor->scene_commands()))
            {
                LogSystem::write(LogLevel::error, "Cannot register render commands");
                return 1;
            }
        }
#if ENGINE_HAS_ASSET_PIPELINE
        if (editor->commands() != nullptr && runtime.world() != nullptr)
        {
            if (!engine::editor::register_mesh_lod_commands(*editor->commands(), pipeline,
                                                            project_root))
            {
                LogSystem::write(LogLevel::error, "Cannot register mesh LOD import commands");
                return 1;
            }
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
            resources, *runtime.renderer(), tasks, *runtime.world(), project_root,
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
            ENGINE_PROFILE_ZONE("Host.frame");
#if defined(ENGINE_ENABLE_TRACY)
            const auto profile_frame_begin = std::chrono::steady_clock::now();
#endif
            frame_engine.begin_frame();
            const auto current_time = std::chrono::steady_clock::now();
            const double delta_time =
                std::min(std::chrono::duration<double>(current_time - previous_time).count(), 0.1);
            previous_time = current_time;
#if ENGINE_HAS_ASSET_PIPELINE
            (void)tasks.dispatch_completions(parallel.completions_per_frame,
                                             parallel.completion_budget_seconds);
#endif
            if (!editor->tick(delta_time))
            {
                LogSystem::write(LogLevel::error, "Editor control frame failed");
                return 1;
            }
#if ENGINE_HAS_UI_EDITOR
            const auto* script_status = runtime.gameplay()->script_status();
            const auto run_id = script_status == nullptr ? 0 : script_status->run_id;
            if (run_id != ui_run_id)
            {
                ui_editor->cancel_pending_input();
                ui_run_id = run_id;
            }
            ui_editor->tick(delta_time);
#endif
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
                ENGINE_PROFILE_ZONE("Editor.modeling_sync");
                modeling->synchronize_scene();
            }
#endif
            auto status = engine::FrameStatus::running;
            if (preview->active())
            {
                status = runtime.poll_events(delta_time);
                if (status == engine::FrameStatus::failed)
                {
                    status = engine::FrameStatus::failed;
                }
                else if (status == engine::FrameStatus::exit_requested)
                {
                    status = engine::FrameStatus::exit_requested;
                }
                else if (!preview->tick(delta_time))
                {
                    LogSystem::write(LogLevel::error, preview->last_error());
                    status = engine::FrameStatus::failed;
                }
                else if (status == engine::FrameStatus::paused)
                {
                    status = engine::FrameStatus::paused;
                }
#if ENGINE_HAS_TERRAIN_PLUGIN
                else if (engine::plugins::terrain::TerrainSystem::instance() &&
                         !engine::plugins::terrain::TerrainSystem::instance()->synchronize())
                {
                    LogSystem::write(LogLevel::error,
                                     "Scene preview terrain synchronization failed");
                    if (const auto* scene = runtime.world()->scene())
                    {
                        std::vector<engine::Node> nodes;
                        if (!scene->collect_nodes(nodes))
                            return 1;
                        for (const auto node : nodes)
                        {
                            const auto terrain =
                                node.get_component<engine::plugins::terrain::TerrainComponent>();
                            if (terrain && !terrain.last_error().empty())
                            {
                                LogSystem::write(LogLevel::error,
                                                 node.name() + ": " + terrain.last_error());
                            }
                        }
                    }
                    preview->close();
                    if (!runtime.renderer()->render())
                    {
                        status = engine::FrameStatus::failed;
                    }
                }
#endif
#if ENGINE_HAS_DESTRUCTION_PLUGIN
                else if (engine::plugins::destruction::DestructionSystem::instance() &&
                         !engine::plugins::destruction::DestructionSystem::instance()
                              ->synchronize())
                {
                    LogSystem::write(LogLevel::error,
                                     "Scene preview destruction synchronization failed");
                    if (const auto* scene = runtime.world()->scene())
                    {
                        std::vector<engine::Node> nodes;
                        if (!scene->collect_nodes(nodes))
                            return 1;
                        for (const auto node : nodes)
                        {
                            const auto destruction = node.get_component<
                                engine::plugins::destruction::DestructionComponent>();
                            if (destruction && !destruction.last_error().empty())
                            {
                                LogSystem::write(LogLevel::error,
                                                 node.name() + ": " + destruction.last_error());
                            }
                        }
                    }
                    preview->close();
                    if (!runtime.renderer()->render())
                    {
                        status = engine::FrameStatus::failed;
                    }
                }
#endif
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
                ENGINE_PROFILE_ZONE("Editor.modeling_sync");
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
#if defined(ENGINE_ENABLE_TRACY)
            ENGINE_PROFILE_PLOT("Host work ms",
                                (std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - profile_frame_begin)
                                     .count()));
#endif
            {
                ENGINE_PROFILE_ZONE("Host.frame_limit_wait");
                frame_engine.end_frame();
            }
            ++frames;
            if (frame_limit != 0 && frames >= frame_limit)
            {
                break;
            }
        }
        return 0;
    }();

    runtime.set_event_observer(nullptr, nullptr);
#if ENGINE_HAS_UI_EDITOR
    if (ui_editor != nullptr)
    {
        ui_editor->cancel_pending_input();
    }
#endif
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
#if ENGINE_HAS_UI_EDITOR
    ui_editor.reset();
#endif
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
    tasks.shutdown();
#endif
    threads.shutdown();
    LogSystem::write(LogLevel::info,
                     "Editor stopped after " + std::to_string(frames) + " rendered frames");
    return result;
}

} // namespace

int main(int argc, char* argv[])
{
    engine::RuntimeSettings settings;
    auto& config = settings.engine;
    auto& parallel = settings.parallel;
    std::optional<std::uint32_t> width_override;
    std::optional<std::uint32_t> height_override;
    std::optional<std::size_t> worker_override;
    std::string runtime_profile = "desktop";
    config.window_title = "Engine Editor";
    std::string project_argument;
    fs::path runtime_directory;
    std::uint64_t frame_limit = 0;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
        if (argument == "--workers" && index + 1 < argc)
        {
            const std::string_view value(argv[++index]);
            std::size_t count = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), count);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || count > 64)
            {
                fmt::print(stderr, "Worker limit must be within 0..64 (0 is automatic)\n");
                return 2;
            }
            worker_override = count;
            continue;
        }
        if (argument == "--runtime-profile" && index + 1 < argc)
        {
            runtime_profile = argv[++index];
            if (runtime_profile != "desktop" && runtime_profile != "mobile")
            {
                print_usage();
                return 2;
            }
            continue;
        }
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
            (argument == "--width" ? width_override : height_override) = pixels;
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
    std::string settings_error;
    if (!engine::editor::load_project_runtime_settings(project_root, runtime_profile, settings,
                                                       settings_error))
    {
        LogSystem::write(LogLevel::error, settings_error);
        log.shutdown();
        environment.shutdown();
        project.shutdown();
        return 1;
    }
    if (width_override)
    {
        config.width = *width_override;
    }
    if (height_override)
    {
        config.height = *height_override;
    }
    if (worker_override)
    {
        parallel.threads.worker_limit = *worker_override;
    }
    const int result = run_editor(project_root, environment, config, frame_limit, parallel);
    const bool flushed = log.flush();
    log.shutdown();
    environment.shutdown();
    project.shutdown();
    return flushed ? result : 1;
}
