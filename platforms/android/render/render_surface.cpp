#include "runtime/render/render_surface.hpp"

#include "android/native/android_surface.hpp"

#include <android/native_window.h>

namespace engine::render_detail
{

bool RenderSurface::prepare(bgfx::Init& init, bool& available)
{
    available = false;
    if (window_ == nullptr || current_.nwh != nullptr || rendering_registered_)
    {
        return false;
    }
    if (!android::begin_rendering())
    {
        // 宿主已暂停；系统继续存活，恢复后重新进入 prepare。
        return true;
    }
    rendering_registered_ = true;
    recreate_on_change_ = true;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bgfx::PlatformData data{};
    if (!read_size(width, height) || !read_platform_data(data))
    {
        release();
        return false;
    }
    // SDL 可以先释放自己的引用；本引用必须保留到 bgfx 渲染线程退出。
    ANativeWindow_acquire(static_cast<ANativeWindow*>(data.nwh));
    current_ = data;
    init.type = bgfx::RendererType::Vulkan;
    init.platformData = data;
    init.resolution.width = width;
    init.resolution.height = height;
    available = true;
    return true;
}

bool RenderSurface::read_platform_data(bgfx::PlatformData& data) const
{
    data = {};
    const auto properties = SDL_GetWindowProperties(window_);
    data.nwh = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
    if (data.nwh == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Native Android surface is unavailable");
        return false;
    }
    return true;
}

void RenderSurface::release() noexcept
{
    if (current_.nwh != nullptr)
    {
        ANativeWindow_release(static_cast<ANativeWindow*>(current_.nwh));
        current_ = {};
    }
    if (rendering_registered_)
    {
        android::end_rendering();
        rendering_registered_ = false;
    }
}

bool RenderSurface::start_watching()
{
    if (!SDL_AddEventWatch(watch_surface_lifecycle, this))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Renderer event watch failed: %s", SDL_GetError());
        return false;
    }
    watching_events_ = true;
    return true;
}

void RenderSurface::stop_watching() noexcept
{
    if (watching_events_)
    {
        SDL_RemoveEventWatch(watch_surface_lifecycle, this);
        watching_events_ = false;
    }
}

bool SDLCALL RenderSurface::watch_surface_lifecycle(void* userdata, SDL_Event* event)
{
    // JNI 暂停握手转为 SDL 主线程事件；其他线程不得关闭 bgfx。
    const bool suspend =
        event->type == SDL_EVENT_WILL_ENTER_BACKGROUND ||
        (event->type == SDL_EVENT_USER && event->user.code == android::suspend_rendering_event);
    if (!suspend || !SDL_IsMainThread())
    {
        return true;
    }
    auto& surface = *static_cast<RenderSurface*>(userdata);
    if (surface.suspend_ != nullptr && !surface.suspending_)
    {
        // 回调可以 release 原生句柄；同一事件链不再次进入关闭流程。
        surface.suspending_ = true;
        surface.suspend_(surface.context_);
        surface.suspending_ = false;
    }
    return true;
}

} // namespace engine::render_detail
