#include "runtime_entry.hpp"

#include "runtime/log/log_system.hpp"
#include "runtime/window/window_system.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <memory>
#include <new>

namespace
{
struct IosAppContext
{
    engine::host_detail::RuntimeAppState runtime;
    std::uint64_t previous_tick = 0;
    bool reset_clock = true;
    bool callbacks_registered = false;
};

// 保持 userdata 存活至进程结束；活动回调中只关闭运行时，不释放上下文。
std::unique_ptr<IosAppContext> application;

void shutdown_ios_app() noexcept;

bool SDLCALL on_lifecycle(void* userdata, SDL_Event* event)
{
    auto& context = *static_cast<IosAppContext*>(userdata);
    if (!SDL_IsMainThread() || !context.runtime.initialized)
    {
        return true;
    }
    if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND ||
        event->type == SDL_EVENT_DID_ENTER_FOREGROUND)
    {
        context.reset_clock = true;
        const bool suspended = event->type == SDL_EVENT_WILL_ENTER_BACKGROUND;
        if (!context.runtime.host.set_suspended(suspended))
        {
            engine::LogSystem::write(engine::LogLevel::error,
                                     "Cannot change the iOS runtime suspension state");
            context.runtime.host.request_exit();
        }
    }
    else if (event->type == SDL_EVENT_TERMINATING)
    {
        shutdown_ios_app();
    }
    return true;
}

void SDLCALL on_frame(void* userdata)
{
    auto& context = *static_cast<IosAppContext*>(userdata);
    if (!context.runtime.initialized)
    {
        return;
    }
    const auto now = SDL_GetTicksNS();
    const double delta =
        context.reset_clock ? 0.0 : static_cast<double>(now - context.previous_tick) / 1.0e9;
    context.previous_tick = now;
    context.reset_clock = false;
    const auto status = engine::host_detail::step_runtime_app(context.runtime, delta);
    if (status == engine::FrameStatus::paused)
    {
        context.reset_clock = true;
    }
    else if (status == engine::FrameStatus::failed || status == engine::FrameStatus::exit_requested)
    {
        if (!context.runtime.error.empty())
        {
            engine::LogSystem::write(engine::LogLevel::error, context.runtime.error);
        }
        shutdown_ios_app();
    }
}

void shutdown_ios_app() noexcept
{
    if (!application)
    {
        return;
    }
    auto& context = *application;
    if (context.callbacks_registered)
    {
        SDL_RemoveEventWatch(on_lifecycle, &context);
        context.callbacks_registered = false;
        auto* windows = context.runtime.host.window();
        if (windows != nullptr && windows->native_window() != nullptr &&
            !SDL_SetiOSAnimationCallback(windows->native_window(), 1, nullptr, nullptr))
        {
            engine::LogSystem::write(engine::LogLevel::error,
                                     "Cannot unregister the iOS animation callback");
        }
    }
    engine::host_detail::shutdown_runtime_app(context.runtime);
}

bool initialize_ios_app(int argc, char** argv)
{
    application.reset(new (std::nothrow) IosAppContext{});
    if (!application)
    {
        return false;
    }
    auto& context = *application;
    engine::AppConfig config;
    config.application_name = ENGINE_PROJECT_APPLICATION_NAME;
    if (!engine::host_detail::initialize_runtime_app(context.runtime, config, argc, argv))
    {
        shutdown_ios_app();
        return false;
    }
    if (context.runtime.exit_requested)
    {
        shutdown_ios_app();
        return true;
    }
    auto* windows = context.runtime.host.window();
    if (windows == nullptr || windows->native_window() == nullptr ||
        !windows->set_mode(engine::WindowMode::borderless))
    {
        shutdown_ios_app();
        return false;
    }
    if (!SDL_AddEventWatch(on_lifecycle, &context))
    {
        shutdown_ios_app();
        return false;
    }
    context.callbacks_registered = true;
    if (!SDL_SetiOSAnimationCallback(windows->native_window(), 1, on_frame, &context))
    {
        shutdown_ios_app();
        return false;
    }
    // UIKit 自己驱动 RunLoop，WindowSystem 保留唯一的 SDL 队列消费入口。
    SDL_SetiOSEventPump(false);
    return true;
}
} // namespace

int main(int argc, char* argv[])
{
    return initialize_ios_app(argc, argv) ? 0 : 1;
}
