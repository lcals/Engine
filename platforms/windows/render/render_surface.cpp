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
    init.type = bgfx::RendererType::Direct3D12;
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
    data.nwh = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (data.nwh == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Native Win32 surface is unavailable");
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
