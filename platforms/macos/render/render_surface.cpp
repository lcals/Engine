#include "runtime/render/render_surface.hpp"

namespace engine::render_detail
{

bool RenderSurface::prepare(bgfx::Init& init, bool& available)
{
    available = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    if (window_ == nullptr || current_.nwh != nullptr || !read_size(width, height))
    {
        return false;
    }
    metal_view_ = SDL_Metal_CreateView(window_);
    if (metal_view_ == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Metal view creation failed: %s", SDL_GetError());
        return false;
    }
    bgfx::PlatformData data{};
    if (!read_platform_data(data))
    {
        release();
        return false;
    }
    current_ = data;
    init.type = bgfx::RendererType::Metal;
    init.platformData = data;
    init.resolution.width = width;
    init.resolution.height = height;
    available = true;
    return true;
}

bool RenderSurface::read_platform_data(bgfx::PlatformData& data) const
{
    data = {};
    data.nwh = metal_view_ != nullptr ? SDL_Metal_GetLayer(metal_view_) : nullptr;
    if (data.nwh == nullptr)
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
}

bool RenderSurface::start_watching()
{
    return true;
}

void RenderSurface::stop_watching() noexcept
{
}

} // namespace engine::render_detail
