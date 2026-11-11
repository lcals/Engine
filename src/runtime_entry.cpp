#include "runtime_entry.hpp"

#if ENGINE_HAS_ANIMATION_RETARGET_PLUGIN
#include "plugins/runtime/animation_retarget/animation_retarget.hpp"
#endif
#include "runtime/app/runtime_host.hpp"
#include "runtime/app/runtime_settings.hpp"
#include "runtime/engine.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/gameplay/deployment_selection.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/movie/movie_player.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime_plugins.hpp"

#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
#include "plugins/runtime/resource_graph/resource_graph_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime_resource_codecs.hpp"
#endif

#if ENGINE_HAS_APP_SERVICES
#include "runtime/app/app.hpp"
#include "runtime/log/log_system.hpp"
#endif

#if ENGINE_HAS_SCRIPT_PLUGIN
#include "plugins/runtime/script/script_system.hpp"
#endif

#if defined(ENGINE_HAS_RUNTIME_MCP_ENDPOINT)
#include "plugins/runtime/mcp/runtime_mcp_endpoint.hpp"
#include "runtime/utils/path_utils.hpp"
#endif

#include <fmt/base.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#if defined(ENGINE_WEB_RENDER_TEST)
namespace engine::test_support
{
[[nodiscard]] bool configure_web_render_test(RuntimeStage stage, WorldSystem& world,
                                             RenderSystem& renderer, WindowSystem& window);
}
#endif

namespace
{
void print_usage()
{
#if defined(ENGINE_HAS_RUNTIME_MCP_ENDPOINT)
    fmt::print("Usage: engine_app --mcp-project <path> --mcp-runtime-dir <empty-cache-path> "
               "[--frames <positive-count>] [--workers <0..64>]\n");
#else
    fmt::print("Usage: engine_app [--frames <positive-count>] [--workers <0..64>]\n");
#endif
}

} // namespace

namespace engine::host_detail
{

bool initialize_runtime_app(RuntimeAppState& state, const AppConfig& requested_app, int argc,
                            char** argv)
{
    const auto fail = [&](std::string_view message)
    {
        state.error = message;
        if (state.exit_code == 0)
        {
            state.exit_code = 1;
        }
        shutdown_runtime_app(state);
        return false;
    };
    auto& settings = state.settings;
    auto& engine = state.host;
    auto& frame_limit = state.frame_limit;
    auto& config = settings.engine;
    auto& parallel = settings.parallel;
    std::optional<std::size_t> worker_override;
#if defined(ENGINE_HAS_RUNTIME_MCP_ENDPOINT)
    std::filesystem::path mcp_project;
    std::filesystem::path mcp_directory;
#endif
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
#if defined(ENGINE_HAS_RUNTIME_MCP_ENDPOINT)
        if ((argument == "--mcp-project" || argument == "--mcp-runtime-dir") && index + 1 < argc)
        {
            auto& value = argument == "--mcp-project" ? mcp_project : mcp_directory;
            if (!value.empty())
            {
                print_usage();
                state.exit_code = 2;
                return fail("Invalid runtime arguments");
            }
            value = engine::PathUtils::from_utf8(argv[++index]);
            continue;
        }
#endif
        if (argument == "--workers" && index + 1 < argc)
        {
            const std::string_view value(argv[++index]);
            std::size_t count = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), count);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || count > 64)
            {
                fmt::print(stderr, "Worker limit must be within 0..64 (0 is automatic)\n");
                state.exit_code = 2;
                return fail("Invalid runtime arguments");
            }
            worker_override = count;
            continue;
        }
        if (argument == "--help" && argc == 2)
        {
            print_usage();
            state.exit_requested = true;
            return true;
        }
        if (argument == "--frames" && index + 1 < argc && frame_limit == 0)
        {
            const std::string_view value(argv[++index]);
            const auto result =
                std::from_chars(value.data(), value.data() + value.size(), frame_limit);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
                frame_limit == 0)
            {
                print_usage();
                state.exit_code = 2;
                return fail("Invalid runtime arguments");
            }
        }
        else
        {
            print_usage();
            state.exit_code = 2;
            return fail("Invalid runtime arguments");
        }
    }
#if defined(ENGINE_HAS_RUNTIME_MCP_ENDPOINT)
    auto& mcp_bridge = state.mcp_bridge;

#endif
#if ENGINE_HAS_APP_SERVICES
    auto app_config = requested_app;
#if defined(ENGINE_HAS_RUNTIME_MCP_ENDPOINT)
    app_config.persistent_data_path = mcp_directory;
    app_config.cache_path = mcp_directory / "runtime-cache";
#endif
    if (!engine::App::initialize(app_config))
    {
        engine::LogSystem::write(engine::LogLevel::error, "Application path initialization failed");
        return fail("Runtime initialization failed");
    }
    state.app_ready = true;
    const auto settings_path = engine::App::assets_path() / ENGINE_RUNTIME_SETTINGS_PATH;
    std::string settings_error;
    if (!engine::load_runtime_settings(settings_path, settings, settings_error))
    {
        engine::LogSystem::write(engine::LogLevel::error, settings_error);
        return fail("Runtime initialization failed");
    }
    engine::App::set_game_language(settings.game_language);
    auto& log = engine::LogSystem::instance();
    if (!log.initialize(config))
    {
        engine::LogSystem::write(engine::LogLevel::error, "Log initialization failed");
        return fail("Runtime initialization failed");
    }
    state.log_ready = true;
#endif
    if (worker_override)
    {
        parallel.threads.worker_limit = *worker_override;
    }
#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
    state.threads = std::make_unique<engine::ThreadSystem>(parallel.threads);
    auto& threads = *state.threads;
    const bool threads_ready = threads.initialize(config);
    const auto domain = [&](engine::TaskConfig value)
    {
        if (!value.max_concurrency)
        {
            value.max_concurrency = threads_ready ? threads.worker_count() : 1;
        }
        return value;
    };
    state.file_tasks = std::make_unique<engine::TaskSystem>(threads, domain(parallel.file));
    state.read_tasks =
        std::make_unique<engine::TaskSystem>(threads, domain(parallel.resource_read));
    state.graph_tasks =
        std::make_unique<engine::TaskSystem>(threads, domain(parallel.resource_graph));
    auto& file_tasks = *state.file_tasks;
    auto& read_tasks = *state.read_tasks;
    auto& graph_tasks = *state.graph_tasks;
    auto& resources = state.resources;
    state.graph = std::make_unique<engine::plugins::resource_graph::ResourceGraphSystem>(
        resources, graph_tasks, engine::plugins::resource_graph::ResourceGraphConfig{});
    auto& resource_graph = *state.graph;
    if (!threads_ready)
    {
        return fail("Thread initialization failed: " + threads.last_error());
    }
    if (!file_tasks.initialize(config))
    {
        return fail("File task initialization failed: " + file_tasks.last_error());
    }
    if (!read_tasks.initialize(config))
    {
        return fail("Read task initialization failed: " + read_tasks.last_error());
    }
    if (!graph_tasks.initialize(config))
    {
        return fail("Graph task initialization failed: " + graph_tasks.last_error());
    }
    if (!register_runtime_resource_codecs(resources))
    {
        return fail("Resource codec registration failed");
    }
    if (!resources.initialize(config))
    {
        return fail("Resource initialization failed: " + std::string(resources.last_error()));
    }
    if (!resources.configure_async(file_tasks, read_tasks, parallel.resources))
    {
        return fail("Async resource initialization failed: " + std::string(resources.last_error()));
    }
    if (!resource_graph.initialize(config))
    {
        return fail("Resource graph initialization failed: " +
                    std::string(resource_graph.last_error()));
    }

#endif
#if ENGINE_HAS_CAMERA
    static engine::plugins::camera::CameraTypeRegistry camera_types;
    if (!camera_types.frozen() &&
        (!engine::plugins::camera::register_builtin_camera_types(camera_types) ||
         !camera_types.freeze()))
    {
        return fail("Runtime initialization failed");
    }
#endif
#if ENGINE_HAS_CHARACTER_CONTROLLER
    static engine::plugins::character_controller::CharacterAbilityRegistry character_abilities;
    if (!character_abilities.frozen() &&
        (!engine::plugins::character_controller::register_builtin_character_abilities(
             character_abilities) ||
         !character_abilities.freeze()))
    {
        return fail("Runtime initialization failed");
    }
#endif
    state.setup = [](engine::RuntimeStage stage, engine::WorldSystem& world,
                     engine::WindowSystem& window, engine::PhysicsSystem& physics,
                     engine::NavigationSystem& navigation, engine::RenderSystem& renderer,
                     engine::TaskSystem& tasks)
    {
        const bool assembled =
            assemble_runtime_plugins(stage, world, window, physics, navigation, renderer, tasks,
#if ENGINE_HAS_CAMERA
                                     &camera_types,
#else
                                     nullptr,
#endif
#if ENGINE_HAS_CHARACTER_CONTROLLER
                                     &character_abilities);
#else
                                     nullptr);
#endif
#if defined(ENGINE_WEB_RENDER_TEST)
        return assembled &&
               engine::test_support::configure_web_render_test(stage, world, renderer, window);
#else
        return assembled;
#endif
    };
    auto& retargeter_factory = state.retargeter_factory;
#if ENGINE_HAS_ANIMATION_RETARGET_PLUGIN
    retargeter_factory = engine::plugins::animation_retarget::create_animation_retargeter;
#endif
    auto& script_factory = state.script_factory;
#if ENGINE_HAS_SCRIPT_PLUGIN
    script_factory = [](engine::WorldSystem& world, engine::RenderSystem& renderer,
                        engine::UiSystem& ui, engine::PhysicsSystem& physics,
                        engine::NavigationSystem& navigation, engine::InputSystem& input,
                        engine::WindowSystem& window, engine::AnimationSystem& animation,
                        engine::MoviePlayer* movies) -> std::unique_ptr<engine::ScriptRuntime>
    {
        return std::make_unique<engine::plugins::script::ScriptSystem>(
            world.resource_loader(), &world, &renderer, &ui, &physics, &navigation, &input, &window,
            &animation, movies);
    };
#endif
#if ENGINE_START_PACKAGED_GAMEPLAY && ENGINE_HAS_RESOURCE_GRAPH_PLUGIN && ENGINE_HAS_APP_SERVICES
    const bool has_intro = !settings.startup_movie.empty();
    constexpr bool staged_startup = true;
#else
    [[maybe_unused]] constexpr bool has_intro = false;
    [[maybe_unused]] constexpr bool staged_startup = false;
#endif
#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
    const bool engine_ready =
        staged_startup ? engine.initialize_presentation(config, &resources, &resource_graph,
                                                        &threads, parallel)
                       : engine.initialize(config, &resources, &resource_graph, script_factory,
                                           state.setup, &threads, parallel, retargeter_factory);
#else
    const bool engine_ready = engine.initialize(config, nullptr, nullptr, script_factory,
                                                state.setup, nullptr, parallel, retargeter_factory);
#endif
    if (!engine_ready)
    {
#if ENGINE_HAS_APP_SERVICES
        engine::LogSystem::write(engine::LogLevel::error, "Engine initialization failed");
#else
        std::fputs("Engine initialization failed\n", stderr);
#endif
        return fail("Runtime initialization failed");
    }
#if ENGINE_START_PACKAGED_GAMEPLAY && ENGINE_HAS_RESOURCE_GRAPH_PLUGIN && ENGINE_HAS_APP_SERVICES
    auto& published = state.published;
    auto& published_result = state.published_result;
    state.has_intro = has_intro;
    state.starting = true;
    const bool starting = state.starting;
    if (starting)
    {
        const auto persistent = engine::App::persistent_data_path();
        const auto builtin = engine::App::assets_path();
        const auto platform = resources.catalog().data().target_platform;
        published_result = engine.tasks()->submit(
            "Startup deployment selection",
            [published, persistent, builtin, platform]()
            {
                const bool selected = engine::gameplay_detail::select_published_deployment(
                    persistent, platform, published->catalog, published->mounts, published->entry,
                    published->warning);
                if (selected && !published->catalog.revision().empty())
                {
                    return true;
                }
                if (!engine::ResourceSystem::read_catalog(builtin, published->catalog))
                {
                    published->error = "Cannot read startup catalog";
                    return false;
                }
                published->mounts = {{"content", builtin}};
                published->entry.clear();
                return true;
            });
        if (!published_result.valid())
        {
            engine::LogSystem::write(engine::LogLevel::error,
                                     "Cannot schedule startup deployment selection");
            return fail("Runtime initialization failed");
        }
    }
#endif
#if defined(ENGINE_HAS_RUNTIME_MCP_ENDPOINT)
    if (!mcp_bridge.initialize(engine, {mcp_project / "cache", mcp_directory, {}}))
    {
        fmt::print(stderr, "Cannot initialize runtime MCP test channel: {}\n",
                   mcp_bridge.last_error());
        return fail("Runtime initialization failed");
    }
#endif
    state.initialized = true;
    return true;
}

FrameStatus step_runtime_app(RuntimeAppState& state, double delta_time)
{
    if (state.exit_requested)
    {
        return FrameStatus::exit_requested;
    }
    const auto fail = [&](std::string_view message)
    {
        state.exit_code = 1;
        state.error = message;
        return FrameStatus::failed;
    };
    if (!state.initialized)
    {
        return fail("Runtime is not initialized");
    }
    auto& engine = state.host;
    auto& frames = state.frames;
    const auto frame_limit = state.frame_limit;
#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
    auto& resources = state.resources;
    auto& resource_graph = *state.graph;
#endif
#if defined(ENGINE_HAS_RUNTIME_MCP_ENDPOINT)
    auto& mcp_bridge = state.mcp_bridge;
#endif
#if ENGINE_START_PACKAGED_GAMEPLAY && ENGINE_HAS_RESOURCE_GRAPH_PLUGIN && ENGINE_HAS_APP_SERVICES
    auto& settings = state.settings;
    auto& starting = state.starting;
    auto& published = state.published;
    auto& published_result = state.published_result;
    auto& startup_catalog = state.startup_catalog;
    auto& startup_mounts = state.startup_mounts;
    auto& intro_handle = state.intro_handle;
    auto& intro_load = state.intro_load;
    auto& intro_active = state.intro_active;
    auto& preparing_gameplay = state.preparing_gameplay;
    auto& gameplay_ready = state.gameplay_ready;
    const auto has_intro = state.has_intro;
    const auto script_factory = state.script_factory;
    const auto retargeter_factory = state.retargeter_factory;
#endif
    ENGINE_PROFILE_ZONE("Host.frame");
#if defined(ENGINE_ENABLE_TRACY)
    const auto profile_frame_begin = std::chrono::steady_clock::now();
#endif
    engine::Engine::instance().begin_frame();
#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
    if (!resources.tick(delta_time) || !resource_graph.tick(delta_time))
    {
        return fail("Runtime frame failed");
    }
#endif
    const auto status = engine.tick(delta_time);
    if (status == engine::FrameStatus::exit_requested)
    {
        return status;
    }
    if (status == engine::FrameStatus::failed)
    {
#if ENGINE_HAS_APP_SERVICES
        engine::LogSystem::write(engine::LogLevel::error, "Engine frame failed");
#else
        std::fputs("Engine frame failed\n", stderr);
#endif
        return fail("Runtime frame failed");
    }
#if defined(ENGINE_HAS_RUNTIME_MCP_ENDPOINT)
    if (!mcp_bridge.poll())
    {
        fmt::print(stderr, "Runtime MCP test channel failed: {}\n", mcp_bridge.last_error());
        return fail("Runtime frame failed");
    }
#endif
    if (status == engine::FrameStatus::paused)
    {
        return status;
    }
#if ENGINE_START_PACKAGED_GAMEPLAY && ENGINE_HAS_RESOURCE_GRAPH_PLUGIN && ENGINE_HAS_APP_SERVICES
    if (starting)
    {
        engine::MoviePlaybackStatus movie_status;
        if (intro_active && engine.movie_player()->status(intro_handle, movie_status))
        {
            if (movie_status.state == engine::MovieState::failed ||
                movie_status.state == engine::MovieState::cancelled)
            {
                engine::LogSystem::write(engine::LogLevel::error, movie_status.error);
                if (!engine.movie_player()->release(intro_handle))
                {
                    return fail("Runtime startup failed");
                }
                intro_active = false;
            }
        }
        if (!preparing_gameplay && published_result.valid() &&
            published_result.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            if (!published_result.get())
            {
                engine::LogSystem::write(engine::LogLevel::error, published->error);
                return fail("Runtime startup failed");
            }
            if (!published->warning.empty())
            {
                engine::LogSystem::write(engine::LogLevel::warning, published->warning);
            }
            *startup_catalog = std::move(published->catalog);
            *startup_mounts = std::move(published->mounts);
            if (!resources.update_catalog(startup_catalog->data(), *startup_mounts))
            {
                engine::LogSystem::write(engine::LogLevel::error, resources.last_error());
                return fail("Runtime startup failed");
            }
            if (has_intro && !resource_graph.begin_load(settings.startup_movie, intro_load))
            {
                engine::LogSystem::write(engine::LogLevel::error, resource_graph.last_error());
            }
            if (!engine.begin_gameplay(script_factory, state.setup, retargeter_factory,
                                       startup_catalog, startup_mounts, published->entry))
            {
                engine::LogSystem::write(engine::LogLevel::error,
                                         "Cannot begin gameplay preparation");
                return fail("Runtime startup failed");
            }
            preparing_gameplay = true;
        }
        if (intro_load)
        {
            engine::ResourceLoadProgress progress;
            const auto load_status = intro_load->status(progress);
            if (load_status == engine::ResourceLoadStatus::ready)
            {
                engine::ResourceHandle source;
                if (intro_load->take(source))
                {
                    intro_active = engine.movie_player()->play(source, intro_handle);
                    (void)resource_graph.release(source);
                }
                if (!intro_active)
                {
                    engine::LogSystem::write(engine::LogLevel::error,
                                             engine.movie_player()->last_error());
                }
                intro_load.reset();
            }
            else if (load_status == engine::ResourceLoadStatus::failed ||
                     load_status == engine::ResourceLoadStatus::cancelled)
            {
                engine::LogSystem::write(engine::LogLevel::error, intro_load->last_error());
                intro_load.reset();
            }
        }
        if (preparing_gameplay && !intro_load && !gameplay_ready)
        {
            const auto ready = engine.step_gameplay(0.002);
            if (ready == engine::RuntimeStartupStatus::failed)
            {
                engine::LogSystem::write(engine::LogLevel::error, "Gameplay preparation failed");
                return fail("Runtime startup failed");
            }
            gameplay_ready = ready == engine::RuntimeStartupStatus::ready;
        }
        if (intro_active && movie_status.state == engine::MovieState::completed)
        {
            if (!engine.movie_player()->release(intro_handle))
            {
                return fail("Runtime startup failed");
            }
            intro_active = false;
        }
        if (gameplay_ready && !intro_active && !intro_load)
        {
            if (!engine.activate_gameplay())
            {
                return fail("Runtime startup failed");
            }
            starting = false;
            engine::LogSystem::write(engine::LogLevel::info, "Packaged gameplay started");
        }
    }
#endif
    ++frames;
    if (frame_limit != 0 && frames >= frame_limit)
    {
        engine.request_exit();
    }
#if defined(ENGINE_ENABLE_TRACY)
    ENGINE_PROFILE_PLOT("Host work ms", (std::chrono::duration<double, std::milli>(
                                             std::chrono::steady_clock::now() - profile_frame_begin)
                                             .count()));
#endif
    {
        ENGINE_PROFILE_ZONE("Host.frame_limit_wait");
        engine::Engine::instance().end_frame();
    }
    return FrameStatus::running;
}

void shutdown_runtime_app(RuntimeAppState& state) noexcept
{
    const bool was_initialized = state.initialized;
#if defined(ENGINE_HAS_RUNTIME_MCP_ENDPOINT)
    state.mcp_bridge.shutdown();
#endif
#if ENGINE_START_PACKAGED_GAMEPLAY && ENGINE_HAS_RESOURCE_GRAPH_PLUGIN && ENGINE_HAS_APP_SERVICES
    state.intro_load.reset();
#endif
    state.host.shutdown();
#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
    if (state.graph)
    {
        state.graph->shutdown();
    }
    state.resources.shutdown();
    if (state.graph_tasks)
    {
        state.graph_tasks->shutdown();
    }
    if (state.read_tasks)
    {
        state.read_tasks->shutdown();
    }
    if (state.file_tasks)
    {
        state.file_tasks->shutdown();
    }
    if (state.threads)
    {
        state.threads->shutdown();
    }
    state.graph.reset();
    state.graph_tasks.reset();
    state.read_tasks.reset();
    state.file_tasks.reset();
    state.threads.reset();
#endif
#if ENGINE_HAS_APP_SERVICES
    if (state.log_ready)
    {
        auto& log = LogSystem::instance();
        if (was_initialized)
        {
            fmt::print("Engine stopped after {} rendered frames\n", state.frames);
            LogSystem::write(LogLevel::info, "Engine stopped after " +
                                                 std::to_string(state.frames) + " rendered frames");
        }
        if (!log.flush())
        {
            state.exit_code = 1;
        }
        log.shutdown();
        state.log_ready = false;
    }
    if (state.app_ready)
    {
        App::shutdown();
        state.app_ready = false;
    }
#endif
    (void)was_initialized;
    state.initialized = false;
}

} // namespace engine::host_detail
