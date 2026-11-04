#include "runtime/render/render_surface.hpp"

namespace engine::render_detail
{

bool RenderSurface::prepare(bgfx::Init& init, bool& available)
{
    available = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bgfx::PlatformData data{};
    bgfx::SwapChain swap_chain = init.swapChain;
    if (window_ == nullptr || current_.nwh != nullptr || !read_size(width, height) ||
        !read_platform_data(data, swap_chain))
    {
        return false;
    }
    swap_chain.width = width;
    swap_chain.height = height;
    current_ = swap_chain;
    platform_data_ = data;
    init.type = bgfx::RendererType::Direct3D12;
    init.platformData = data;
    init.swapChain = swap_chain;
    available = true;
    return true;
}

bool RenderSurface::read_platform_data(bgfx::PlatformData& data, bgfx::SwapChain& swap_chain) const
{
    data = {};
    swap_chain.nwh = nullptr;
    swap_chain.ndt = nullptr;
    const auto properties = SDL_GetWindowProperties(window_);
    swap_chain.nwh =
        SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (swap_chain.nwh == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Native Win32 surface is unavailable");
        return false;
    }
    return true;
}

void RenderSurface::release() noexcept
{
    current_ = {};
    platform_data_ = {};
}

bool RenderSurface::start_watching()
{
    return true;
}

void RenderSurface::stop_watching() noexcept
{
}

} // namespace engine::render_detail
