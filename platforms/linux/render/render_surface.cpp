#include "runtime/render/render_surface.hpp"

namespace engine::render_detail
{

bool RenderSurface::prepare(bgfx::Init& init, bool& available)
{
    available = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bgfx::PlatformData data{};
    if (window_ == nullptr || current_.nwh != nullptr || !read_size(width, height) ||
        !read_platform_data(data))
    {
        return false;
    }
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
    if (data.ndt == nullptr || data.nwh == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Native Wayland/X11 surface is unavailable");
        data = {};
        return false;
    }
    return true;
}

void RenderSurface::release() noexcept
{
    current_ = {};
}

bool RenderSurface::start_watching()
{
    return true;
}

void RenderSurface::stop_watching() noexcept
{
}

} // namespace engine::render_detail
