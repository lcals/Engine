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

#include <SDL3/SDL_main.h>
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

int main(int argc, char* argv[])
{
    engine::RuntimeSettings settings;
    auto& config = settings.engine;
    auto& parallel = settings.parallel;
    std::optional<std::size_t> worker_override;
    std::uint64_t frame_limit = 0;
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
                return 2;
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
                return 2;
            }
            worker_override = count;
            continue;
        }
        if (argument == "--help" && argc == 2)
        {
            print_usage();
            return 0;
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
                return 2;
            }
        }
        else
        {
            print_usage();
            return 2;
        }
    }
    engine::RuntimeHost engine;
#if defined(ENGINE_HAS_RUNTIME_MCP_ENDPOINT)
    engine::plugins::mcp::RuntimeMcpEndpoint mcp_bridge;

#endif
#if ENGINE_HAS_APP_SERVICES
    engine::AppConfig app_config{};
#if defined(ENGINE_PROJECT_APPLICATION_NAME)
    app_config.application_name = ENGINE_PROJECT_APPLICATION_NAME;
#endif
#if defined(ENGINE_HAS_RUNTIME_MCP_ENDPOINT)
    app_config.persistent_data_path = mcp_directory;
    app_config.cache_path = mcp_directory / "runtime-cache";
#endif
    if (!engine::App::initialize(app_config))
    {
        engine::LogSystem::write(engine::LogLevel::error, "Application path initialization failed");
        return 1;
    }
    const auto settings_path = engine::App::assets_path() / ENGINE_RUNTIME_SETTINGS_PATH;
    std::string settings_error;
    if (!engine::load_runtime_settings(settings_path, settings, settings_error))
    {
        engine::LogSystem::write(engine::LogLevel::error, settings_error);
        engine::App::shutdown();
        return 1;
    }
    engine::App::set_game_language(settings.game_language);
    auto& log = engine::LogSystem::instance();
    if (!log.initialize(config))
    {
        engine::LogSystem::write(engine::LogLevel::error, "Log initialization failed");
        engine::App::shutdown();
        return 1;
    }
#endif
    if (worker_override)
    {
        parallel.threads.worker_limit = *worker_override;
    }
#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
    engine::ThreadSystem threads(parallel.threads);
    const bool threads_ready = threads.initialize(config);
    const auto domain = [&](engine::TaskConfig value)
    {
        if (!value.max_concurrency)
        {
            value.max_concurrency = threads_ready ? threads.worker_count() : 1;
        }
        return value;
    };
    engine::TaskSystem file_tasks(threads, domain(parallel.file));
    engine::TaskSystem read_tasks(threads, domain(parallel.resource_read));
    engine::TaskSystem graph_tasks(threads, domain(parallel.resource_graph));
    engine::ResourceSystem resources;
    engine::plugins::resource_graph::ResourceGraphSystem resource_graph(resources, graph_tasks, {});
    const auto shutdown_resources = [&]()
    {
        resource_graph.shutdown();
        resources.shutdown();
        graph_tasks.shutdown();
        read_tasks.shutdown();
        file_tasks.shutdown();
        threads.shutdown();
    };
    if (!threads_ready || !file_tasks.initialize(config) || !read_tasks.initialize(config) ||
        !graph_tasks.initialize(config) || !register_runtime_resource_codecs(resources) ||
        !resources.initialize(config) ||
        !resources.configure_async(file_tasks, read_tasks, parallel.resources) ||
        !resource_graph.initialize(config))
    {
        shutdown_resources();
#if ENGINE_HAS_APP_SERVICES
        engine::LogSystem::write(engine::LogLevel::error,
                                 "Resource services initialization failed");
        log.shutdown();
        engine::App::shutdown();
#else
        std::fputs("Resource services initialization failed\n", stderr);
#endif
        return 1;
    }
#endif
#if ENGINE_HAS_CAMERA
    static engine::plugins::camera::CameraTypeRegistry camera_types;
    if (!camera_types.frozen() &&
        (!engine::plugins::camera::register_builtin_camera_types(camera_types) ||
         !camera_types.freeze()))
    {
        return 1;
    }
#endif
#if ENGINE_HAS_CHARACTER_CONTROLLER
    static engine::plugins::character_controller::CharacterAbilityRegistry character_abilities;
    if (!character_abilities.frozen() &&
        (!engine::plugins::character_controller::register_builtin_character_abilities(
             character_abilities) ||
         !character_abilities.freeze()))
    {
        return 1;
    }
#endif
    const engine::RuntimeSetup configure_runtime_plugins =
        [](engine::RuntimeStage stage, engine::WorldSystem& world, engine::WindowSystem& window,
           engine::PhysicsSystem& physics, engine::NavigationSystem& navigation,
           engine::RenderSystem& renderer, engine::TaskSystem& tasks)
    {
        return assemble_runtime_plugins(stage, world, window, physics, navigation, renderer, tasks,
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
    };
    engine::AnimationRetargeterFactory retargeter_factory = nullptr;
#if ENGINE_HAS_ANIMATION_RETARGET_PLUGIN
    retargeter_factory = engine::plugins::animation_retarget::create_animation_retargeter;
#endif
    engine::ScriptRuntimeFactory script_factory = nullptr;
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
        staged_startup
            ? engine.initialize_presentation(config, &resources, &resource_graph, &threads,
                                             parallel)
            : engine.initialize(config, &resources, &resource_graph, script_factory,
                                configure_runtime_plugins, &threads, parallel, retargeter_factory);
#else
    const bool engine_ready =
        engine.initialize(config, nullptr, nullptr, script_factory, configure_runtime_plugins,
                          nullptr, parallel, retargeter_factory);
#endif
    if (!engine_ready)
    {
#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
        shutdown_resources();
#endif
#if ENGINE_HAS_APP_SERVICES
        engine::LogSystem::write(engine::LogLevel::error, "Engine initialization failed");
        log.shutdown();
        engine::App::shutdown();
#else
        std::fputs("Engine initialization failed\n", stderr);
#endif
        return 1;
    }
#if ENGINE_START_PACKAGED_GAMEPLAY && ENGINE_HAS_RESOURCE_GRAPH_PLUGIN && ENGINE_HAS_APP_SERVICES
    struct StartupDirectory
    {
        engine::ResourceCatalog catalog;
        engine::MountList mounts;
        std::string entry;
        std::string error;
        std::string warning;
    };
    auto startup_catalog = std::make_shared<engine::ResourceCatalog>();
    auto startup_mounts = std::make_shared<engine::MountList>();
    auto published = std::make_shared<StartupDirectory>();
    std::future<bool> published_result;
    engine::MoviePlaybackHandle intro_handle;
    std::unique_ptr<engine::ResourceLoad> intro_load;
    bool intro_active = false;
    bool preparing_gameplay = false;
    bool gameplay_ready = false;
    bool starting = true;
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
            engine.shutdown();
            shutdown_resources();
            log.shutdown();
            engine::App::shutdown();
            return 1;
        }
    }
#endif
#if defined(ENGINE_HAS_RUNTIME_MCP_ENDPOINT)
    if (!mcp_bridge.initialize(engine, {mcp_project / "cache", mcp_directory, {}}))
    {
        fmt::print(stderr, "Cannot initialize runtime MCP test channel: {}\n",
                   mcp_bridge.last_error());
        return 1;
    }
#endif
    auto previous_time = std::chrono::steady_clock::now();
    std::uint64_t frames = 0;
    int result = 0;
    for (;;)
    {
        ENGINE_PROFILE_ZONE("Host.frame");
#if defined(ENGINE_ENABLE_TRACY)
        const auto profile_frame_begin = std::chrono::steady_clock::now();
#endif
        engine::Engine::instance().begin_frame();
        const auto current_time = std::chrono::steady_clock::now();
        const double delta_time =
            std::chrono::duration<double>(current_time - previous_time).count();
        previous_time = current_time;
#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
        if (!resources.tick(delta_time) || !resource_graph.tick(delta_time))
        {
            result = 1;
            break;
        }
#endif
        const auto status = engine.tick(delta_time);
        if (status == engine::FrameStatus::exit_requested)
        {
            break;
        }
        if (status == engine::FrameStatus::failed)
        {
#if ENGINE_HAS_APP_SERVICES
            engine::LogSystem::write(engine::LogLevel::error, "Engine frame failed");
#else
            std::fputs("Engine frame failed\n", stderr);
#endif
            result = 1;
            break;
        }
#if defined(ENGINE_HAS_RUNTIME_MCP_ENDPOINT)
        if (!mcp_bridge.poll())
        {
            fmt::print(stderr, "Runtime MCP test channel failed: {}\n", mcp_bridge.last_error());
            result = 1;
            break;
        }
#endif
        if (status == engine::FrameStatus::paused)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            previous_time = std::chrono::steady_clock::now();
            continue;
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
                        result = 1;
                        break;
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
                    result = 1;
                    break;
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
                    result = 1;
                    break;
                }
                if (has_intro && !resource_graph.begin_load(settings.startup_movie, intro_load))
                {
                    engine::LogSystem::write(engine::LogLevel::error, resource_graph.last_error());
                }
                if (!engine.begin_gameplay(script_factory, configure_runtime_plugins,
                                           retargeter_factory, startup_catalog, startup_mounts,
                                           published->entry))
                {
                    engine::LogSystem::write(engine::LogLevel::error,
                                             "Cannot begin gameplay preparation");
                    result = 1;
                    break;
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
                    engine::LogSystem::write(engine::LogLevel::error,
                                             "Gameplay preparation failed");
                    result = 1;
                    break;
                }
                gameplay_ready = ready == engine::RuntimeStartupStatus::ready;
            }
            if (intro_active && movie_status.state == engine::MovieState::completed)
            {
                if (!engine.movie_player()->release(intro_handle))
                {
                    result = 1;
                    break;
                }
                intro_active = false;
            }
            if (gameplay_ready && !intro_active && !intro_load)
            {
                if (!engine.activate_gameplay())
                {
                    result = 1;
                    break;
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
        ENGINE_PROFILE_PLOT("Host work ms",
                            (std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - profile_frame_begin)
                                 .count()));
#endif
        {
            ENGINE_PROFILE_ZONE("Host.frame_limit_wait");
            engine::Engine::instance().end_frame();
        }
    }
#if ENGINE_START_PACKAGED_GAMEPLAY
    intro_load.reset();
#endif
#if defined(ENGINE_HAS_RUNTIME_MCP_ENDPOINT)
    mcp_bridge.shutdown();
#endif
    engine.shutdown();
#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
    shutdown_resources();
#endif
    fmt::print("Engine stopped after {} rendered frames\n", frames);
#if ENGINE_HAS_APP_SERVICES
    engine::LogSystem::write(engine::LogLevel::info,
                             "Engine stopped after " + std::to_string(frames) + " rendered frames");
    if (!log.flush())
    {
        result = 1;
    }
    log.shutdown();
    engine::App::shutdown();
#endif
    return result;
}
