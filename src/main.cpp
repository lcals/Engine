#if ENGINE_HAS_ANIMATION_RETARGET_PLUGIN
#include "plugins/runtime/animation_retarget/animation_retarget.hpp"
#endif
#include "runtime/app/runtime_host.hpp"
#include "runtime/app/runtime_settings.hpp"
#include "runtime/engine.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime_plugins.hpp"

#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
#include "plugins/runtime/resource_graph/resource_graph_system.hpp"
#include "runtime/resource/resource_system.hpp"
#endif

#if ENGINE_HAS_APP_SERVICES
#include "runtime/app/app.hpp"
#include "runtime/log/log_system.hpp"
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
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace
{
#if ENGINE_START_PACKAGED_GAMEPLAY
constexpr bool packaged_gameplay = true;
#else
constexpr bool packaged_gameplay = false;
#endif

void print_usage()
{
    fmt::print("Usage: engine_app [--frames <positive-count>] [--workers <0..64>] "
               "[--runtime-profile <desktop|mobile>]\n");
}

} // namespace

int main(int argc, char* argv[])
{
    engine::RuntimeSettings settings;
    auto& config = settings.engine;
    auto& parallel = settings.parallel;
    std::optional<std::size_t> worker_override;
    std::string runtime_profile = ENGINE_RUNTIME_PROFILE;
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
#if ENGINE_HAS_APP_SERVICES
    engine::AppConfig app_config{};
#if defined(ENGINE_PROJECT_APPLICATION_NAME)
    app_config.application_name = ENGINE_PROJECT_APPLICATION_NAME;
#endif
    if (!engine::App::initialize(app_config))
    {
        engine::LogSystem::write(engine::LogLevel::error, "Application path initialization failed");
        return 1;
    }
    const auto settings_path = engine::App::assets_path() / ENGINE_RUNTIME_SETTINGS_PATH;
    std::string settings_error;
    // APK settings are required by the platform package; native filesystem probes cannot see them.
    bool read_settings = packaged_gameplay || ENGINE_RUNTIME_SETTINGS_REQUIRED;
    if (!read_settings)
    {
        std::error_code status;
        read_settings = std::filesystem::exists(settings_path, status) || status;
    }
    if (read_settings &&
        !engine::load_runtime_settings(settings_path, runtime_profile, settings, settings_error))
    {
        engine::LogSystem::write(engine::LogLevel::error, settings_error);
        engine::App::shutdown();
        return 1;
    }
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
    engine::ResourceSystem resources;
    engine::plugins::resource_graph::ResourceGraphSystem resource_graph(resources);
    const auto shutdown_resources = [&]()
    {
        resources.set_loader(nullptr);
        resource_graph.shutdown();
        resources.shutdown();
    };
    if (!resources.initialize(config) || !resource_graph.initialize(config))
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
    resources.set_loader(&resource_graph);
#endif
    engine::RuntimeHost engine;
    engine::AnimationRetargeterFactory retargeter_factory = nullptr;
#if ENGINE_HAS_ANIMATION_RETARGET_PLUGIN
    retargeter_factory = engine::plugins::animation_retarget::create_animation_retargeter;
#endif
    engine::ScriptRuntimeFactory script_factory = nullptr;
#if ENGINE_HAS_SCRIPT_PLUGIN
    script_factory = [](engine::WorldSystem& world, engine::RenderSystem& renderer,
                        engine::UiSystem& ui, engine::PhysicsSystem& physics,
                        engine::NavigationSystem& navigation, engine::InputSystem& input,
                        engine::WindowSystem& window,
                        engine::AnimationSystem& animation) -> std::unique_ptr<engine::ScriptRuntime>
    {
        return std::make_unique<engine::plugins::script::ScriptSystem>(
            &world, &renderer, &ui, &physics, &navigation, &input, &window, &animation);
    };
#endif
#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
    const bool engine_ready = engine.initialize(config, &resources, script_factory,
                                                configure_runtime_plugins, nullptr, parallel,
                                                retargeter_factory);
#else
    const bool engine_ready = engine.initialize(config, nullptr, script_factory,
                                                configure_runtime_plugins, nullptr, parallel,
                                                retargeter_factory);
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
#if ENGINE_START_PACKAGED_GAMEPLAY
    if (!engine.gameplay()->start())
    {
        const std::string detail(engine.gameplay()->last_error());
        engine::LogSystem::write(engine::LogLevel::error, "Gameplay startup failed: " + detail);
        engine.shutdown();
#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
        shutdown_resources();
#endif
        log.shutdown();
        engine::App::shutdown();
        return 1;
    }
    engine::LogSystem::write(engine::LogLevel::info, "Packaged gameplay started");
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
            std::min(std::chrono::duration<double>(current_time - previous_time).count(), 0.1);
        previous_time = current_time;
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
        if (status == engine::FrameStatus::paused)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            previous_time = std::chrono::steady_clock::now();
            continue;
        }
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
