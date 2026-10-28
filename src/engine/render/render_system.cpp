#include "render_system.hpp"

#include "window_system.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#if defined(SDL_PLATFORM_ANDROID)
#include "android_surface.hpp"

#include <android/native_window.h>
#endif

#include <limits>

namespace engine
{
namespace
{

// SDL 窗口和 bgfx 生命周期都由主线程驱动；bgfx 只支持一个活动实例。
RenderSystem* active_renderer = nullptr;
constexpr std::uint16_t main_view = 0;
constexpr std::uint32_t reset_flags = BGFX_RESET_VSYNC;

bool window_size(SDL_Window* window, std::uint32_t& width, std::uint32_t& height)
{
    int pixel_width = 0;
    int pixel_height = 0;
    if (!SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot query drawable size: %s", SDL_GetError());
        return false;
    }
    constexpr int max_size = std::numeric_limits<std::uint16_t>::max();
    if (pixel_width <= 0 || pixel_height <= 0 || pixel_width > max_size || pixel_height > max_size)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unsupported drawable size: %d x %d", pixel_width,
                     pixel_height);
        return false;
    }
    width = static_cast<std::uint32_t>(pixel_width);
    height = static_cast<std::uint32_t>(pixel_height);
    return true;
}

bool platform_data(SDL_Window* window, void* metal_view, bgfx::PlatformData& data)
{
#if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
    (void)window;
    data.nwh = SDL_Metal_GetLayer(metal_view);
#else
    (void)metal_view;
    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
#if defined(SDL_PLATFORM_WINDOWS)
    data.nwh = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(SDL_PLATFORM_ANDROID)
    data.nwh = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
#elif defined(SDL_PLATFORM_LINUX)
    data.ndt = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
    if (data.ndt != nullptr)
    {
        data.nwh =
            SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        data.type = bgfx::NativeWindowHandleType::Wayland;
    }
    else
    {
        data.ndt = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        const auto handle = static_cast<std::uintptr_t>(
            SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
        data.nwh = reinterpret_cast<void*>(handle);
    }
#else
    (void)properties;
#endif
#endif
    if (data.nwh == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Native window surface is unavailable");
        return false;
    }
    return true;
}

} // namespace

RenderSystem::RenderSystem(WindowSystem& windows) noexcept : windows_(windows)
{
}

RenderSystem::~RenderSystem() noexcept
{
    shutdown();
}

bool RenderSystem::on_initialize(const EngineConfig&)
{
#if defined(SDL_PLATFORM_ANDROID)
    if (!SDL_AddEventWatch(watch_surface_lifecycle, this))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Renderer event watch failed: %s", SDL_GetError());
        return false;
    }
    watching_events_ = true;
#endif
    return initialize_renderer();
}

bool RenderSystem::watch_surface_lifecycle(void* userdata, SDL_Event* event)
{
#if defined(SDL_PLATFORM_ANDROID)
    // 原生后台事件和平台暂停握手均在 SDL 主线程处理。
    const bool suspend =
        event->type == SDL_EVENT_WILL_ENTER_BACKGROUND ||
        (event->type == SDL_EVENT_USER && event->user.code == android::suspend_rendering_event);
    if (suspend && SDL_IsMainThread())
    {
        static_cast<RenderSystem*>(userdata)->shutdown_renderer();
    }
#else
    (void)userdata;
    (void)event;
#endif
    return true;
}

bool RenderSystem::initialize_renderer()
{
    if (active_renderer != nullptr || !windows_.is_initialized() ||
        windows_.native_window() == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "Renderer requires a ready window and no active bgfx");
        return false;
    }

#if defined(SDL_PLATFORM_ANDROID)
    if (!android::begin_rendering())
    {
        // 宿主在初始化途中进入后台；保留系统，等恢复后再创建 GPU 资源。
        return true;
    }
    surface_registered_ = true;
#endif

    SDL_Window* window = windows_.native_window();
    if (!window_size(window, width_, height_))
    {
        return false;
    }

    bgfx::Init init{};
#if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
    metal_view_ = SDL_Metal_CreateView(window);
    if (metal_view_ == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Metal view creation failed: %s", SDL_GetError());
        return false;
    }
    init.type = bgfx::RendererType::Metal;
#elif defined(SDL_PLATFORM_WINDOWS)
    init.type = bgfx::RendererType::Direct3D12;
#else
    init.type = bgfx::RendererType::Vulkan;
#endif
    if (!platform_data(window, metal_view_, init.platformData))
    {
        return false;
    }
    native_window_ = init.platformData.nwh;
    native_display_ = init.platformData.ndt;
#if defined(SDL_PLATFORM_ANDROID)
    // SDL 可以先释放自己的引用；bgfx 后台线程退出后才释放渲染器持有的引用。
    ANativeWindow_acquire(static_cast<ANativeWindow*>(native_window_));
#endif
    init.resolution.width = width_;
    init.resolution.height = height_;
    init.resolution.reset = reset_flags;
    if (!bgfx::init(init))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "bgfx initialization failed");
        return false;
    }
    renderer_initialized_ = true;
    active_renderer = this;
    if (bgfx::getRendererType() != init.type)
    {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER, "Required renderer %s is unavailable; bgfx selected %s",
            bgfx::getRendererName(init.type), bgfx::getRendererName(bgfx::getRendererType()));
        return false;
    }

    bgfx::setViewName(main_view, "Main window");
    bgfx::setViewClear(main_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x182432ff, 1.0F, 0);
    bgfx::setDebug(BGFX_DEBUG_TEXT);
    SDL_Log("Renderer initialized: %s (%u x %u)", bgfx::getRendererName(bgfx::getRendererType()),
            width_, height_);
    return true;
}

bool RenderSystem::render()
{
    if (!is_initialized() || !windows_.can_render())
    {
        return false;
    }

#if defined(SDL_PLATFORM_ANDROID)
    if (!renderer_initialized_)
    {
        if (!initialize_renderer() || !renderer_initialized_)
        {
            return false;
        }
    }
#endif

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    SDL_Window* window = windows_.native_window();
    bgfx::PlatformData data{};
    if (!window_size(window, width, height) || !platform_data(window, metal_view_, data))
    {
        return false;
    }
    if (data.ndt != native_display_)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Native display changed; renderer restart required");
        return false;
    }

    const bool surface_changed = data.nwh != native_window_;
    if (surface_changed)
    {
#if defined(SDL_PLATFORM_ANDROID)
        shutdown_renderer();
        if (!initialize_renderer() || !renderer_initialized_)
        {
            return false;
        }
#else
        // bgfx 允许初始化后替换原生窗口句柄；显示连接和图形设备保持不变。
        bgfx::setPlatformData(data);
        native_window_ = data.nwh;
#endif
    }
    if (surface_changed || width != width_ || height != height_)
    {
        bgfx::reset(width, height, reset_flags);
        width_ = width;
        height_ = height;
    }

    bgfx::setViewRect(main_view, 0, 0, static_cast<std::uint16_t>(width_),
                      static_cast<std::uint16_t>(height_));
    bgfx::touch(main_view);
    bgfx::dbgTextClear();
    bgfx::dbgTextPrintf(2, 2, 0x0f, "Engine - minimal runtime");
    bgfx::dbgTextPrintf(2, 4, 0x07, "Renderer: %s", bgfx::getRendererName(bgfx::getRendererType()));
    bgfx::dbgTextPrintf(2, 5, 0x07, "Drawable: %u x %u", width_, height_);
#if defined(SDL_PLATFORM_ANDROID)
    bgfx::dbgTextPrintf(2, 7, 0x07, "Press Back to exit; Home pauses rendering.");
#else
    bgfx::dbgTextPrintf(2, 7, 0x07, "Resize this window or close it to exit.");
#endif
    bgfx::frame();
    return true;
}

void RenderSystem::on_shutdown() noexcept
{
    if (watching_events_)
    {
#if defined(SDL_PLATFORM_ANDROID)
        SDL_RemoveEventWatch(watch_surface_lifecycle, this);
#endif
        watching_events_ = false;
    }
    shutdown_renderer();
}

void RenderSystem::shutdown_renderer() noexcept
{
    if (renderer_initialized_)
    {
        bgfx::shutdown();
        renderer_initialized_ = false;
        active_renderer = nullptr;
    }
#if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
    if (metal_view_ != nullptr)
    {
        SDL_Metal_DestroyView(metal_view_);
        metal_view_ = nullptr;
    }
#endif
#if defined(SDL_PLATFORM_ANDROID)
    if (native_window_ != nullptr)
    {
        ANativeWindow_release(static_cast<ANativeWindow*>(native_window_));
    }
#endif
    native_window_ = nullptr;
    native_display_ = nullptr;
    width_ = 0;
    height_ = 0;
    if (surface_registered_)
    {
#if defined(SDL_PLATFORM_ANDROID)
        android::end_rendering();
#endif
        surface_registered_ = false;
    }
}

} // namespace engine
