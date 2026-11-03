#include "runtime/render/passes/planar_reflection_pass.hpp"
#include "runtime/render/passes/planar_reflection_math.hpp"
#include "runtime/render/render_sampling.hpp"
#include "runtime/scene/camera_math.hpp"
#include <algorithm>
#include <bx/math.h>
#include <cmath>

namespace engine::render_detail
{
PlanarReflectionPass::~PlanarReflectionPass() noexcept
{
    release();
}

bool PlanarReflectionPass::prepare(std::uint16_t new_width, std::uint16_t new_height,
                                   std::uint8_t samples)
{
    if (new_width == width && new_height == height && samples_ == samples &&
        bgfx::isValid(framebuffer_))
    {
        return true;
    }
    release();
    const auto* caps = bgfx::getCaps();
    if (!caps || !valid_msaa_samples(samples) || new_width == 0 || new_height == 0 ||
        new_width > caps->limits.maxTextureSize || new_height > caps->limits.maxTextureSize ||
        caps->limits.maxFBAttachments < 3)
    {
        return false;
    }
    const auto flags = render_target_flags(samples) | BGFX_SAMPLER_UVW_CLAMP;
    color_ =
        bgfx::createTexture2D(new_width, new_height, false, 1, bgfx::TextureFormat::RGBA16F, flags);
    specular_ =
        bgfx::createTexture2D(new_width, new_height, false, 1, bgfx::TextureFormat::RGBA16F, flags);
    depth_ = bgfx::createTexture2D(new_width, new_height, false, 1, bgfx::TextureFormat::D32F,
                                   render_target_flags(samples) | BGFX_TEXTURE_RT_WRITE_ONLY);
    if (bgfx::isValid(color_) && bgfx::isValid(specular_) && bgfx::isValid(depth_))
    {
        const std::array attachments = {color_, specular_, depth_};
        framebuffer_ = bgfx::createFrameBuffer(3, attachments.data(), false);
    }
    if (!bgfx::isValid(framebuffer_))
    {
        release();
        return false;
    }
    width = new_width;
    height = new_height;
    samples_ = samples;
    return true;
}

bool PlanarReflectionPass::mirror(const float* plane_world, const float* camera_world,
                                  const float* camera_projection, float clip_bias)
{
    const auto* caps = bgfx::getCaps();
    return caps != nullptr && prepare_planar_reflection(
                                  plane_world, camera_world, camera_projection, clip_bias,
                                  caps->homogeneousDepth, world, view, projection, view_projection);
}

void PlanarReflectionPass::begin(bgfx::ViewId view_id, std::uint32_t clear_color)
{
    attached_view_ = view_id;
    // Each view has its own palette slot; subsequent views cannot change this clear.
    const auto palette = static_cast<std::uint8_t>(2 + view_id / 40);
    std::array<float, 4> clear{};
    for (std::size_t index = 0; index < 3; ++index)
    {
        clear[index] =
            std::pow(static_cast<float>((clear_color >> ((3 - index) * 8)) & 255) / 255, 2.2F);
    }
    clear[3] = 1;
    bgfx::setPaletteColor(palette, clear.data());
    bgfx::setViewName(view_id, "Planar reflection capture");
    bgfx::setViewFrameBuffer(view_id, framebuffer_);
    bgfx::setViewRect(view_id, 0, 0, width, height);
    bgfx::setViewClear(view_id, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.0F, 0, palette, 1);
    bgfx::setViewTransform(view_id, view.data(), projection.data());
    bgfx::touch(view_id);
}

void PlanarReflectionPass::release() noexcept
{
    if (bgfx::isValid(framebuffer_))
    {
        if (attached_view_ != UINT16_MAX)
        {
            bgfx::setViewFrameBuffer(attached_view_, BGFX_INVALID_HANDLE);
        }
        bgfx::destroy(framebuffer_);
        framebuffer_ = BGFX_INVALID_HANDLE;
    }
    for (auto* handle : {&color_, &specular_, &depth_})
    {
        if (bgfx::isValid(*handle))
        {
            bgfx::destroy(*handle);
            *handle = BGFX_INVALID_HANDLE;
        }
    }
    width = height = 0;
    node_id = 0;
    attached_view_ = UINT16_MAX;
    samples_ = 1;
}
std::uint32_t PlanarReflectionPass::resource_count() const noexcept
{
    return static_cast<std::uint32_t>(bgfx::isValid(color_)) +
           static_cast<std::uint32_t>(bgfx::isValid(specular_)) +
           static_cast<std::uint32_t>(bgfx::isValid(depth_)) +
           static_cast<std::uint32_t>(bgfx::isValid(framebuffer_));
}
} // namespace engine::render_detail
