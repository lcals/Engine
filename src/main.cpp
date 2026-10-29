#include "runtime/engine.hpp"
#include "runtime/engine_config.hpp"

#if ENGINE_HAS_APP_SERVICES
#include "runtime/app/app.hpp"
#include "runtime/log/log_system.hpp"
#endif

#if ENGINE_START_PACKAGED_GAMEPLAY
#include "runtime/script/script_system.hpp"
#endif

#include <SDL3/SDL_main.h>
#include <fmt/base.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <thread>

namespace
{

void print_usage()
{
    fmt::print("Usage: engine_app [--frames <positive-count>]\n");
}

} // namespace

int main(int argc, char* argv[])
{
    engine::EngineConfig config;
    std::uint64_t frame_limit = 0;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
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
    if (!engine::App::initialize(engine::AppConfig{}))
    {
        engine::LogSystem::write(engine::LogLevel::error, "Application path initialization failed");
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
    auto& engine = engine::Engine::instance();
    if (!engine.initialize(config))
    {
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
    if (!engine.start_gameplay())
    {
        const auto* status = engine.script_status();
        const std::string detail =
            status != nullptr ? status->last_error : "script system unavailable";
        engine::LogSystem::write(engine::LogLevel::error, "Gameplay startup failed: " + detail);
        engine.shutdown();
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
    }
    engine.shutdown();
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
