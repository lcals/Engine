#include "window_system.hpp"

#include "engine_config.hpp"

#include <SDL3/SDL.h>

#include <limits>

namespace engine
{
namespace
{

constexpr SDL_InitFlags sdl_flags = SDL_INIT_VIDEO | SDL_INIT_EVENTS;

bool SDLCALL watch_application_state(void* userdata, SDL_Event* event)
{
    auto& background = *static_cast<std::atomic<bool>*>(userdata);
    switch (event->type)
    {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        background.store(true, std::memory_order_relaxed);
        break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        background.store(false, std::memory_order_relaxed);
        break;
    default:
        break;
    }
    return true;
}

} // namespace

WindowSystem::WindowSystem() noexcept = default;

WindowSystem::~WindowSystem() noexcept
{
    shutdown();
}

bool WindowSystem::exit_requested() const noexcept
{
    return exit_requested_;
}

bool WindowSystem::can_render() const noexcept
{
    if (!is_initialized() || window_ == nullptr || exit_requested_ ||
        background_.load(std::memory_order_relaxed))
    {
        return false;
    }

    const SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
    if ((flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) != 0)
    {
        return false;
    }

#if defined(SDL_PLATFORM_ANDROID)
    const SDL_PropertiesID properties = SDL_GetWindowProperties(window_);
    if (SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr) ==
        nullptr)
    {
        return false;
    }
#endif

    int width = 0;
    int height = 0;
    return SDL_GetWindowSizeInPixels(window_, &width, &height) && width > 0 && height > 0;
}

SDL_Window* WindowSystem::native_window() const noexcept
{
    return window_;
}

bool WindowSystem::on_initialize(const EngineConfig& config)
{
    constexpr auto max_size = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    if (config.width == 0 || config.height == 0 || config.width > max_size ||
        config.height > max_size)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid window dimensions");
        return false;
    }

    if (!SDL_InitSubSystem(sdl_flags))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL initialization failed: %s", SDL_GetError());
        return false;
    }
    sdl_initialized_ = true;
    exit_requested_ = false;
    background_.store(false, std::memory_order_relaxed);

    if (!SDL_AddEventWatch(watch_application_state, &background_))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL event watch failed: %s", SDL_GetError());
        return false;
    }
    watching_events_ = true;

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
    flags |= SDL_WINDOW_METAL;
#endif
    window_ = SDL_CreateWindow(config.window_title.c_str(), static_cast<int>(config.width),
                               static_cast<int>(config.height), flags);
    if (window_ == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL window creation failed: %s",
                     SDL_GetError());
        return false;
    }
    return true;
}

bool WindowSystem::on_tick(double)
{
    if (window_ == nullptr)
    {
        return false;
    }

    const SDL_WindowID window_id = SDL_GetWindowID(window_);
    SDL_Event event{};
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == window_id))
        {
            exit_requested_ = true;
        }
    }
    return true;
}

void WindowSystem::on_shutdown() noexcept
{
    if (watching_events_)
    {
        SDL_RemoveEventWatch(watch_application_state, &background_);
        watching_events_ = false;
    }
    if (window_ != nullptr)
    {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    if (sdl_initialized_)
    {
        SDL_QuitSubSystem(sdl_flags);
        sdl_initialized_ = false;
    }
    background_.store(false, std::memory_order_relaxed);
    exit_requested_ = false;
}

} // namespace engine
