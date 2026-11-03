#include "runtime/render/passes/surface_pass.hpp"

#include "runtime/render/render_constants.hpp"

#include <algorithm>

namespace engine::render_detail
{

SurfacePass::~SurfacePass() noexcept
{
    release();
}

bool SurfacePass::prepare(std::uint16_t width, std::uint16_t height)
{
    if (bgfx::isValid(framebuffer_) && width == width_ && height == height_)
    {
        return true;
    }
    release();
    const auto* caps = bgfx::getCaps();
    constexpr auto flags =
        BGFX_TEXTURE_RT | BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
    if (width == 0 || height == 0 || caps->limits.maxFBAttachments < 3 ||
        width > caps->limits.maxTextureSize || height > caps->limits.maxTextureSize ||
        !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA16F, flags) ||
        !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D32F, flags))
    {
        return false;
    }
    targets_[0] =
        bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::RGBA16F, flags);
    targets_[1] =
        bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::RGBA16F, flags);
    targets_[2] = bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::D32F, flags);
    if (!std::all_of(targets_.begin(), targets_.end(), [](auto h) { return bgfx::isValid(h); }))
    {
        release();
        return false;
    }
    framebuffer_ =
        bgfx::createFrameBuffer(static_cast<std::uint8_t>(targets_.size()), targets_.data(), false);
    if (!bgfx::isValid(framebuffer_))
    {
        release();
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

void SurfacePass::begin(const float* view, const float* projection)
{
    for (auto id : {ambient_occlusion_depth_view, surface_view})
    {
        bgfx::setViewName(id, id == surface_view ? "Material surface" : "Scene depth");
        bgfx::setViewFrameBuffer(id, framebuffer_);
        bgfx::setViewRect(id, 0, 0, width_, height_);
        bgfx::setViewTransform(id, view, projection);
        bgfx::setViewMode(id, bgfx::ViewMode::Sequential);
        bgfx::setViewClear(
            id, id == surface_view ? BGFX_CLEAR_NONE : BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0, 1.0F,
            0);
        bgfx::touch(id);
    }
}

bgfx::TextureHandle SurfacePass::depth() const noexcept
{
    return targets_[2];
}

bgfx::TextureHandle SurfacePass::normal_roughness() const noexcept
{
    return targets_[0];
}

bgfx::TextureHandle SurfacePass::specular() const noexcept
{
    return targets_[1];
}

std::uint32_t SurfacePass::resource_count() const noexcept
{
    return static_cast<std::uint32_t>(bgfx::isValid(framebuffer_)) +
           static_cast<std::uint32_t>(std::count_if(targets_.begin(), targets_.end(),
                                                    [](auto h) { return bgfx::isValid(h); }));
}

void SurfacePass::release() noexcept
{
    if (bgfx::isValid(framebuffer_))
    {
        for (auto view : {ambient_occlusion_depth_view, surface_view})
        {
            bgfx::setViewFrameBuffer(view, BGFX_INVALID_HANDLE);
        }
        bgfx::destroy(framebuffer_);
        framebuffer_ = BGFX_INVALID_HANDLE;
    }
    for (auto& target : targets_)
    {
        if (bgfx::isValid(target))
        {
            bgfx::destroy(target);
            target = BGFX_INVALID_HANDLE;
        }
    }
    width_ = height_ = 0;
}

} // namespace engine::render_detail
