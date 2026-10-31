#include "runtime/render/render_surface.hpp"

#include <limits>

namespace engine::render_detail
{

RenderSurface::~RenderSurface() noexcept
{
    shutdown();
}

bool RenderSurface::initialize(SDL_Window& window, void* context, SuspendCallback suspend)
{
    if (window_ != nullptr || context == nullptr || suspend == nullptr)
    {
        return false;
    }
    window_ = &window;
    context_ = context;
    suspend_ = suspend;
    if (!start_watching())
    {
        shutdown();
        return false;
    }
    return true;
}

SurfaceChange RenderSurface::poll(bgfx::PlatformData& data, std::uint32_t& width,
                                  std::uint32_t& height)
{
    data = {};
    width = 0;
    height = 0;
    bgfx::PlatformData candidate{};
    std::uint32_t candidate_width = 0;
    std::uint32_t candidate_height = 0;
    if (window_ == nullptr || current_.nwh == nullptr ||
        !read_size(candidate_width, candidate_height) || !read_platform_data(candidate))
    {
        return SurfaceChange::error;
    }
    if (candidate.ndt != current_.ndt || candidate.type != current_.type)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Native display changed; renderer restart required");
        return SurfaceChange::error;
    }
    data = candidate;
    width = candidate_width;
    height = candidate_height;
    if (candidate.nwh == current_.nwh)
    {
        return SurfaceChange::unchanged;
    }
    if (recreate_on_change_)
    {
        return SurfaceChange::recreate;
    }
    current_ = candidate;
    return SurfaceChange::rebind;
}

void RenderSurface::shutdown() noexcept
{
    stop_watching();
    release();
    suspend_ = nullptr;
    context_ = nullptr;
    window_ = nullptr;
}

bool RenderSurface::read_size(std::uint32_t& width, std::uint32_t& height) const
{
    width = 0;
    height = 0;
    int pixel_width = 0;
    int pixel_height = 0;
    if (window_ == nullptr || !SDL_GetWindowSizeInPixels(window_, &pixel_width, &pixel_height))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot query drawable size: %s", SDL_GetError());
        return false;
    }
    constexpr int maximum = std::numeric_limits<std::uint16_t>::max();
    if (pixel_width <= 0 || pixel_height <= 0 || pixel_width > maximum || pixel_height > maximum)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unsupported drawable size: %d x %d", pixel_width,
                     pixel_height);
        return false;
    }
    width = static_cast<std::uint32_t>(pixel_width);
    height = static_cast<std::uint32_t>(pixel_height);
    return true;
}

} // namespace engine::render_detail
