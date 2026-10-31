#include "runtime/render/hdr_post_process_pass.hpp"

#include "runtime/render/render_system.hpp"

#include <SDL3/SDL.h>

#include "fs_hdr_blur.bin.h"
#include "fs_hdr_bright.bin.h"
#include "fs_hdr_resolve.bin.h"
#include "vs_hdr.bin.h"

#include <algorithm>
#include <cmath>

namespace engine
{
namespace
{

constexpr bgfx::ViewId scene_view = 6;
constexpr bgfx::ViewId bright_view = 7;
constexpr bgfx::ViewId blur_view = 8;
constexpr bgfx::ViewId resolve_view = 9;
constexpr std::uint8_t clear_palette = 0;
constexpr std::uint32_t linear_clamp = BGFX_SAMPLER_UVW_CLAMP;
constexpr std::uint64_t color_flags = BGFX_TEXTURE_RT | linear_clamp;
constexpr std::uint64_t depth_flags = BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY;

bool supports_hdr(const bgfx::Caps& caps, std::uint16_t width, std::uint16_t height)
{
    constexpr auto color_capabilities =
        BGFX_CAPS_FORMAT_TEXTURE_2D | BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER;
    if (width == 0 || height == 0 || width > caps.limits.maxTextureSize ||
        height > caps.limits.maxTextureSize || caps.limits.maxTextureSamplers < 2)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "HDR target dimensions or sampler limits are invalid (%u x %u, max=%u)",
                     static_cast<unsigned int>(width), static_cast<unsigned int>(height),
                     caps.limits.maxTextureSize);
        return false;
    }
    if ((caps.formats[bgfx::TextureFormat::RGBA16F] & color_capabilities) != color_capabilities ||
        !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA16F, color_flags) ||
        !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D32F, depth_flags))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "HDR requires sampled RGBA16F and D32F render targets (color=0x%x depth=0x%x)",
                     static_cast<unsigned int>(caps.formats[bgfx::TextureFormat::RGBA16F]),
                     static_cast<unsigned int>(caps.formats[bgfx::TextureFormat::D32F]));
        return false;
    }
    return true;
}

bool valid_settings(const RenderSettings& settings)
{
    return settings.hdr_enabled && std::isfinite(settings.exposure) && settings.exposure > 0.0F &&
           settings.exposure <= 100.0F && std::isfinite(settings.bloom_threshold) &&
           settings.bloom_threshold >= 0.0F && settings.bloom_threshold <= 100.0F &&
           std::isfinite(settings.bloom_intensity) && settings.bloom_intensity >= 0.0F &&
           settings.bloom_intensity <= 10.0F && std::isfinite(settings.bloom_radius) &&
           settings.bloom_radius >= 0.25F && settings.bloom_radius <= 4.0F;
}

void set_post_view(bgfx::ViewId view, const char* name, bgfx::FrameBufferHandle framebuffer,
                   std::uint16_t width, std::uint16_t height)
{
    bgfx::setViewName(view, name);
    bgfx::setViewFrameBuffer(view, framebuffer);
    bgfx::setViewRect(view, 0, 0, width, height);
    bgfx::setViewClear(view, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(view, nullptr, nullptr);
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
}

} // namespace

HdrPostProcessPass::~HdrPostProcessPass() noexcept
{
    release();
}

bool HdrPostProcessPass::initialize_resources()
{
    if (std::all_of(programs_.begin(), programs_.end(),
                    [](auto handle) { return bgfx::isValid(handle); }) &&
        bgfx::isValid(fullscreen_) && bgfx::isValid(black_) && bgfx::isValid(parameters_) &&
        bgfx::isValid(texel_))
    {
        return true;
    }
    release();
    // Frame ordering and fullscreen sampling adapt bgfx examples/09-hdr at
    // 7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099. Keep radiance in RGBA16F rather than RGBE8.
    // Copyright 2011-2025 Branimir Karadzic; BSD-2-Clause, third_party/notices/bgfx-hdr.md.
    struct Bytecode
    {
        const std::uint8_t* data;
        std::uint32_t size;
    };
    const std::array fragments = {
        Bytecode{fs_hdr_bright, static_cast<std::uint32_t>(sizeof(fs_hdr_bright))},
        Bytecode{fs_hdr_blur, static_cast<std::uint32_t>(sizeof(fs_hdr_blur))},
        Bytecode{fs_hdr_resolve, static_cast<std::uint32_t>(sizeof(fs_hdr_resolve))}};
    const auto vertex =
        bgfx::createShader(bgfx::copy(vs_hdr, static_cast<std::uint32_t>(sizeof(vs_hdr))));
    if (!bgfx::isValid(vertex))
    {
        return false;
    }
    bool complete = true;
    for (std::size_t index = 0; index < programs_.size(); ++index)
    {
        const auto fragment =
            bgfx::createShader(bgfx::copy(fragments[index].data, fragments[index].size));
        if (!bgfx::isValid(fragment))
        {
            complete = false;
            break;
        }
        programs_[index] = bgfx::createProgram(vertex, fragment, false);
        bgfx::destroy(fragment);
        if (!bgfx::isValid(programs_[index]))
        {
            complete = false;
            break;
        }
    }
    bgfx::destroy(vertex);
    if (!complete)
    {
        return false;
    }
    constexpr std::array sampler_names = {"s_hdr_scene", "s_hdr_bloom"};
    for (std::size_t index = 0; index < samplers_.size(); ++index)
    {
        samplers_[index] = bgfx::createUniform(sampler_names[index], bgfx::UniformType::Sampler);
        if (!bgfx::isValid(samplers_[index]))
        {
            return false;
        }
    }
    parameters_ = bgfx::createUniform("u_hdr_params", bgfx::UniformType::Vec4);
    texel_ = bgfx::createUniform("u_hdr_texel", bgfx::UniformType::Vec4);
    struct Vertex
    {
        float x;
        float y;
        float z;
        float u;
        float v;
    };
    std::array vertices = {Vertex{-1.0F, -1.0F, 0.0F, 0.0F, 1.0F},
                           Vertex{3.0F, -1.0F, 0.0F, 2.0F, 1.0F},
                           Vertex{-1.0F, 3.0F, 0.0F, 0.0F, -1.0F}};
    if (bgfx::getCaps()->originBottomLeft)
    {
        for (auto& vertex_data : vertices)
        {
            vertex_data.v = 1.0F - vertex_data.v;
        }
    }
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    fullscreen_ = bgfx::createVertexBuffer(bgfx::copy(vertices.data(), sizeof(vertices)), layout);
    constexpr std::uint32_t black = 0;
    black_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, linear_clamp,
                                   bgfx::copy(&black, sizeof(black)));
    return bgfx::isValid(parameters_) && bgfx::isValid(texel_) && bgfx::isValid(fullscreen_) &&
           bgfx::isValid(black_);
}

bool HdrPostProcessPass::prepare(std::uint16_t width, std::uint16_t height, bool bloom_enabled)
{
    const auto* caps = bgfx::getCaps();
    if (caps == nullptr || !supports_hdr(*caps, width, height))
    {
        release();
        return false;
    }
    if (!initialize_resources())
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot initialize HDR post-process programs");
        release();
        return false;
    }
    if (width_ != width || height_ != height || !bgfx::isValid(framebuffer_))
    {
        release_targets();
        width_ = width;
        height_ = height;
        color_ = bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::RGBA16F,
                                       color_flags);
        depth_ =
            bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::D32F, depth_flags);
        if (bgfx::isValid(color_) && bgfx::isValid(depth_))
        {
            const std::array attachments = {color_, depth_};
            framebuffer_ = bgfx::createFrameBuffer(2, attachments.data(), false);
        }
        if (!bgfx::isValid(framebuffer_))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot allocate HDR scene target");
            release();
            return false;
        }
    }
    if (!bloom_enabled)
    {
        release_bloom();
        return true;
    }
    if (std::all_of(bloom_framebuffers_.begin(), bloom_framebuffers_.end(),
                    [](auto handle) { return bgfx::isValid(handle); }))
    {
        return true;
    }
    release_bloom();
    constexpr std::array<std::uint32_t, 2> divisors{2, 8};
    for (std::size_t index = 0; index < bloom_colors_.size(); ++index)
    {
        bloom_widths_[index] = static_cast<std::uint16_t>(
            (static_cast<std::uint32_t>(width) + divisors[index] - 1) / divisors[index]);
        bloom_heights_[index] = static_cast<std::uint16_t>(
            (static_cast<std::uint32_t>(height) + divisors[index] - 1) / divisors[index]);
        bloom_colors_[index] =
            bgfx::createTexture2D(bloom_widths_[index], bloom_heights_[index], false, 1,
                                  bgfx::TextureFormat::RGBA16F, color_flags);
        if (bgfx::isValid(bloom_colors_[index]))
        {
            bloom_framebuffers_[index] = bgfx::createFrameBuffer(1, &bloom_colors_[index], false);
        }
        if (!bgfx::isValid(bloom_framebuffers_[index]))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot allocate HDR bloom targets");
            release();
            return false;
        }
    }
    return true;
}

void HdrPostProcessPass::begin_scene(std::uint32_t clear_color)
{
    std::array<float, 4> clear{};
    for (std::size_t index = 0; index < 4; ++index)
    {
        const auto shift = static_cast<unsigned int>((3 - index) * 8);
        clear[index] = static_cast<float>((clear_color >> shift) & 0xffU) / 255.0F;
        if (index != 3)
        {
            clear[index] = std::pow(clear[index], 2.2F);
        }
    }
    bgfx::setPaletteColor(clear_palette, clear.data());
    bgfx::setViewName(scene_view, "HDR scene");
    bgfx::setViewFrameBuffer(scene_view, framebuffer_);
    bgfx::setViewRect(scene_view, 0, 0, width_, height_);
    bgfx::setViewClear(scene_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.0F, 0, clear_palette);
    bgfx::touch(scene_view);
}

bool HdrPostProcessPass::resolve(const RenderSettings& settings)
{
    if (!valid_settings(settings) || width_ == 0 || height_ == 0 || !bgfx::isValid(framebuffer_) ||
        (settings.bloom_enabled &&
         !std::all_of(bloom_framebuffers_.begin(), bloom_framebuffers_.end(),
                      [](auto handle) { return bgfx::isValid(handle); })))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "HDR resolve requires prepared targets and valid settings");
        return false;
    }
    const float parameters[] = {settings.exposure, settings.filmic_tonemap ? 1.0F : 0.0F,
                                settings.bloom_enabled ? settings.bloom_intensity : 0.0F,
                                settings.bloom_threshold};
    const auto submit = [&](bgfx::ViewId view, std::size_t program, bgfx::TextureHandle source,
                            const std::array<float, 4>& texel)
    {
        bgfx::setUniform(parameters_, parameters);
        bgfx::setUniform(texel_, texel.data());
        bgfx::setTexture(0, samplers_[0], source, linear_clamp);
        if (program == 2)
        {
            bgfx::setTexture(1, samplers_[1], settings.bloom_enabled ? bloom_colors_[1] : black_,
                             linear_clamp);
        }
        bgfx::setVertexBuffer(0, fullscreen_);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(view, programs_[program]);
    };
    std::array<float, 4> bloom_texel{};
    if (settings.bloom_enabled)
    {
        bloom_texel = {settings.bloom_radius / bloom_widths_[1],
                       settings.bloom_radius / bloom_heights_[1], 0.0F, 0.0F};
        set_post_view(bright_view, "Bloom bright extraction", bloom_framebuffers_[0],
                      bloom_widths_[0], bloom_heights_[0]);
        submit(bright_view, 0, color_, {1.0F / width_, 1.0F / height_, 0.0F, 0.0F});
        set_post_view(blur_view, "Bloom vertical blur", bloom_framebuffers_[1], bloom_widths_[1],
                      bloom_heights_[1]);
        submit(blur_view, 1, bloom_colors_[0], bloom_texel);
    }
    set_post_view(resolve_view, "HDR resolve and bloom horizontal blur", BGFX_INVALID_HANDLE,
                  width_, height_);
    submit(resolve_view, 2, color_, bloom_texel);
    return true;
}

std::uint32_t HdrPostProcessPass::resource_count() const noexcept
{
    std::uint32_t count = static_cast<std::uint32_t>(bgfx::isValid(parameters_)) +
                          static_cast<std::uint32_t>(bgfx::isValid(texel_)) +
                          static_cast<std::uint32_t>(bgfx::isValid(fullscreen_)) +
                          static_cast<std::uint32_t>(bgfx::isValid(black_)) +
                          static_cast<std::uint32_t>(bgfx::isValid(color_)) +
                          static_cast<std::uint32_t>(bgfx::isValid(depth_)) +
                          static_cast<std::uint32_t>(bgfx::isValid(framebuffer_));
    for (auto handle : programs_)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(handle));
    }
    for (auto handle : samplers_)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(handle));
    }
    for (auto handle : bloom_colors_)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(handle));
    }
    for (auto handle : bloom_framebuffers_)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(handle));
    }
    return count;
}

void HdrPostProcessPass::release_bloom() noexcept
{
    for (std::size_t index = 0; index < bloom_framebuffers_.size(); ++index)
    {
        if (bgfx::isValid(bloom_framebuffers_[index]))
        {
            bgfx::setViewFrameBuffer(static_cast<bgfx::ViewId>(bright_view + index),
                                     BGFX_INVALID_HANDLE);
            bgfx::destroy(bloom_framebuffers_[index]);
            bloom_framebuffers_[index] = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(bloom_colors_[index]))
        {
            bgfx::destroy(bloom_colors_[index]);
            bloom_colors_[index] = BGFX_INVALID_HANDLE;
        }
    }
    bloom_widths_ = {};
    bloom_heights_ = {};
}

void HdrPostProcessPass::release_targets() noexcept
{
    release_bloom();
    if (bgfx::isValid(framebuffer_))
    {
        bgfx::setViewFrameBuffer(scene_view, BGFX_INVALID_HANDLE);
        bgfx::setViewName(scene_view, "Main window");
        bgfx::destroy(framebuffer_);
        framebuffer_ = BGFX_INVALID_HANDLE;
    }
    for (auto* handle : {&color_, &depth_})
    {
        if (bgfx::isValid(*handle))
        {
            bgfx::destroy(*handle);
            *handle = BGFX_INVALID_HANDLE;
        }
    }
    width_ = 0;
    height_ = 0;
}

void HdrPostProcessPass::release() noexcept
{
    release_targets();
    for (auto& handle : programs_)
    {
        if (bgfx::isValid(handle))
        {
            bgfx::destroy(handle);
            handle = BGFX_INVALID_HANDLE;
        }
    }
    for (auto& handle : samplers_)
    {
        if (bgfx::isValid(handle))
        {
            bgfx::destroy(handle);
            handle = BGFX_INVALID_HANDLE;
        }
    }
    for (auto* handle : {&parameters_, &texel_})
    {
        if (bgfx::isValid(*handle))
        {
            bgfx::destroy(*handle);
            *handle = BGFX_INVALID_HANDLE;
        }
    }
    if (bgfx::isValid(fullscreen_))
    {
        bgfx::destroy(fullscreen_);
        fullscreen_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(black_))
    {
        bgfx::destroy(black_);
        black_ = BGFX_INVALID_HANDLE;
    }
}

} // namespace engine
