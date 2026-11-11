#include "runtime/render/render_surface.hpp"

namespace engine::render_detail
{

bool RenderSurface::prepare(bgfx::Init& init, bool& available)
{
    available = false;
    if (window_ == nullptr || current_.nwh != nullptr)
    {
        return false;
    }
    if ((SDL_GetWindowFlags(window_) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) != 0)
    {
        return true;
    }
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    if (!read_size(width, height))
    {
        return false;
    }
    // Metal layer 身份变化时复用完整重建流程，不依赖 bgfx 内部强制 reset 标志。
    recreate_on_change_ = true;
    metal_view_ = SDL_Metal_CreateView(window_);
    if (metal_view_ == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Metal view creation failed: %s", SDL_GetError());
        return false;
    }
    bgfx::PlatformData data{};
    bgfx::SwapChain swap_chain = init.swapChain;
    if (!read_platform_data(data, swap_chain))
    {
        release();
        return false;
    }
    swap_chain.width = width;
    swap_chain.height = height;
    current_ = swap_chain;
    platform_data_ = data;
    init.type = bgfx::RendererType::Metal;
    init.platformData = data;
    init.swapChain = swap_chain;
    available = true;
    return true;
}

bool RenderSurface::read_platform_data(bgfx::PlatformData& data, bgfx::SwapChain& swap_chain) const
{
    data = {};
    swap_chain.ndt = nullptr;
    swap_chain.nwh = metal_view_ != nullptr ? SDL_Metal_GetLayer(metal_view_) : nullptr;
    if (swap_chain.nwh == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Native Metal surface is unavailable");
        return false;
    }
    return true;
}

void RenderSurface::release() noexcept
{
    if (metal_view_ != nullptr)
    {
        SDL_Metal_DestroyView(metal_view_);
        metal_view_ = nullptr;
    }
    current_ = {};
    platform_data_ = {};
}

bool RenderSurface::start_watching()
{
    if (!SDL_AddEventWatch(watch_surface_lifecycle, this))
    {
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
    if (event->type != SDL_EVENT_WILL_ENTER_BACKGROUND || !SDL_IsMainThread())
    {
        return true;
    }
    auto& surface = *static_cast<RenderSurface*>(userdata);
    if (surface.suspend_ != nullptr && !surface.suspending_)
    {
        surface.suspending_ = true;
        surface.suspend_(surface.context_);
        surface.suspending_ = false;
    }
    return true;
}

} // namespace engine::render_detail
