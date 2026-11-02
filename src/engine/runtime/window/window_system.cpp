#include "runtime/window/window_system.hpp"

#include "runtime/engine_config.hpp"

#include <SDL3/SDL.h>

#if defined(SDL_PLATFORM_ANDROID)
#include "android/native/android_surface.hpp"
#endif

#include <cmath>
#include <limits>

namespace engine
{
namespace
{

constexpr SDL_InitFlags sdl_flags = SDL_INIT_VIDEO | SDL_INIT_EVENTS;

bool valid_mode(const SDL_DisplayMode& mode)
{
    return mode.w > 0 && mode.h > 0 && std::isfinite(mode.pixel_density) &&
           mode.pixel_density > 0.0F && std::isfinite(mode.refresh_rate) &&
           mode.refresh_rate >= 0.0F;
}

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

bool WindowSystem::request_exit() noexcept
{
    if (!is_initialized() || window_ == nullptr || !SDL_IsMainThread())
    {
        return false;
    }
    exit_requested_ = true;
    return true;
}

bool WindowSystem::size(WindowSize& output) const noexcept
{
    output = {};
    if (!is_initialized() || window_ == nullptr || !SDL_IsMainThread())
    {
        return false;
    }
    int width = 0;
    int height = 0;
    int pixel_width = 0;
    int pixel_height = 0;
    if (!SDL_GetWindowSize(window_, &width, &height) ||
        !SDL_GetWindowSizeInPixels(window_, &pixel_width, &pixel_height) || width <= 0 ||
        height <= 0 || pixel_width <= 0 || pixel_height <= 0)
    {
        return false;
    }
    output = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
              static_cast<std::uint32_t>(pixel_width), static_cast<std::uint32_t>(pixel_height)};
    return true;
}

bool WindowSystem::display_modes(std::vector<DisplayMode>& output) const
{
    output.clear();
    if (!is_initialized() || window_ == nullptr || !SDL_IsMainThread())
    {
        return false;
    }
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
    if (display == 0)
    {
        return false;
    }
    int count = 0;
    auto** modes = SDL_GetFullscreenDisplayModes(display, &count);
    if (modes == nullptr)
    {
        return false;
    }
    bool valid = count >= 0;
    for (int index = 0; valid && index < count; ++index)
    {
        const auto* value = modes[index];
        valid = value != nullptr && valid_mode(*value);
        if (valid)
        {
            output.push_back({static_cast<std::uint32_t>(value->w),
                              static_cast<std::uint32_t>(value->h), value->pixel_density,
                              value->refresh_rate});
        }
    }
    SDL_free(modes);
    if (!valid)
    {
        output.clear();
    }
    return valid;
}

bool WindowSystem::mode(WindowMode& output) const noexcept
{
    output = WindowMode::windowed;
    if (!is_initialized() || window_ == nullptr || !SDL_IsMainThread())
    {
        return false;
    }
    if ((SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0)
    {
        output = SDL_GetWindowFullscreenMode(window_) == nullptr ? WindowMode::borderless
                                                                : WindowMode::fullscreen;
    }
    return true;
}

bool WindowSystem::set_size(std::uint32_t logical_width, std::uint32_t logical_height)
{
    constexpr auto maximum = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    WindowMode current;
    if (logical_width == 0 || logical_height == 0 || logical_width > maximum ||
        logical_height > maximum || !mode(current) || current != WindowMode::windowed)
    {
        return false;
    }
    return SDL_SetWindowSize(window_, static_cast<int>(logical_width),
                             static_cast<int>(logical_height));
}

bool WindowSystem::set_mode(WindowMode requested, const DisplayMode* exclusive_mode)
{
    if (!is_initialized() || window_ == nullptr || !SDL_IsMainThread())
    {
        return false;
    }
    if (requested == WindowMode::windowed || requested == WindowMode::borderless)
    {
        if (exclusive_mode != nullptr)
        {
            return false;
        }
        if (requested == WindowMode::windowed)
        {
            return SDL_SetWindowFullscreen(window_, false);
        }
        return SDL_SetWindowFullscreenMode(window_, nullptr) &&
               SDL_SetWindowFullscreen(window_, true);
    }
    if (requested != WindowMode::fullscreen || exclusive_mode == nullptr ||
        exclusive_mode->width == 0 || exclusive_mode->height == 0 ||
        !std::isfinite(exclusive_mode->pixel_density) || exclusive_mode->pixel_density <= 0.0F ||
        !std::isfinite(exclusive_mode->refresh_rate) || exclusive_mode->refresh_rate < 0.0F)
    {
        return false;
    }
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
    if (display == 0)
    {
        return false;
    }
    int count = 0;
    auto** modes = SDL_GetFullscreenDisplayModes(display, &count);
    if (modes == nullptr)
    {
        return false;
    }
    bool accepted = false;
    for (int index = 0; index < count; ++index)
    {
        const auto* value = modes[index];
        if (value != nullptr && valid_mode(*value) &&
            static_cast<std::uint32_t>(value->w) == exclusive_mode->width &&
            static_cast<std::uint32_t>(value->h) == exclusive_mode->height &&
            value->pixel_density == exclusive_mode->pixel_density &&
            value->refresh_rate == exclusive_mode->refresh_rate)
        {
            accepted = SDL_SetWindowFullscreenMode(window_, value);
            break;
        }
    }
    SDL_free(modes);
    return accepted && SDL_SetWindowFullscreen(window_, true);
}

bool WindowSystem::mouse_mode(MouseMode& output) const noexcept
{
    output = MouseMode::normal;
    if (!is_initialized() || window_ == nullptr || !SDL_IsMainThread())
    {
        return false;
    }
    output = SDL_GetWindowRelativeMouseMode(window_) ? MouseMode::relative
             : SDL_CursorVisible()                 ? MouseMode::normal
                                                   : MouseMode::hidden;
    return true;
}

bool WindowSystem::set_mouse_mode(MouseMode requested)
{
    if (!is_initialized() || window_ == nullptr || !SDL_IsMainThread())
    {
        return false;
    }
    switch (requested)
    {
    case MouseMode::relative:
        return SDL_SetWindowRelativeMouseMode(window_, true);
    case MouseMode::normal:
        return SDL_SetWindowRelativeMouseMode(window_, false) && SDL_ShowCursor();
    case MouseMode::hidden:
        return SDL_SetWindowRelativeMouseMode(window_, false) && SDL_HideCursor();
    }
    return false;
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
    if (android::surface_paused())
    {
        return false;
    }
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

void WindowSystem::set_event_handler(WindowEventHandler handler, void* context) noexcept
{
    event_handler_ = handler;
    event_context_ = context;
}

bool WindowSystem::on_initialize(const EngineConfig& config)
{
    if (config.window_title.empty())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Window title must not be empty");
        return false;
    }
    constexpr auto max_size = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    if (config.width == 0 || config.height == 0 || config.width > max_size ||
        config.height > max_size)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid window dimensions");
        return false;
    }

#if defined(SDL_PLATFORM_ANDROID)
    // 后台仍泵送主线程回调，Java 暂停握手才不会等在 SDL 自带的暂停阻塞上。
    if (!SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0") ||
        !SDL_SetHint(SDL_HINT_ANDROID_ALLOW_RECREATE_ACTIVITY, "1"))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot configure Android lifecycle handling");
        return false;
    }
#endif
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
#elif defined(SDL_PLATFORM_ANDROID)
    flags |= SDL_WINDOW_VULKAN;
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
        if (event_handler_ != nullptr)
        {
            event_handler_(event_context_, event);
        }

        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == window_id))
        {
            exit_requested_ = true;
        }
#if defined(SDL_PLATFORM_ANDROID)
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_AC_BACK)
        {
            exit_requested_ = true;
        }
#endif
    }
    return true;
}

void WindowSystem::on_shutdown() noexcept
{
    set_event_handler(nullptr, nullptr);
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
