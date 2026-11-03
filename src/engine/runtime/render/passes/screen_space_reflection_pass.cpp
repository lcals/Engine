#include "runtime/render/passes/screen_space_reflection_pass.hpp"

#include "runtime/render/render_math.hpp"
#include "runtime/render/render_settings.hpp"

#include "fs_ssr_composite.bin.h"
#include "fs_ssr_reduce.bin.h"
#include "fs_ssr_trace.bin.h"
#include "vs_hdr.bin.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>

namespace engine::render_detail
{
namespace
{
constexpr bgfx::ViewId first_depth_view = 114;
constexpr bgfx::ViewId trace_view = 126;
constexpr bgfx::ViewId composite_view = 127;
constexpr std::uint32_t point_clamp = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_MIN_POINT |
                                      BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;

bool valid_settings(const RenderSettings& settings)
{
    return std::isfinite(settings.ssr_resolution_scale) && settings.ssr_resolution_scale >= 0.25F &&
           settings.ssr_resolution_scale <= 1.0F && settings.ssr_max_steps >= 8 &&
           settings.ssr_max_steps <= 256 && std::isfinite(settings.ssr_max_distance) &&
           settings.ssr_max_distance > 0.0F && settings.ssr_max_distance <= 100000.0F &&
           std::isfinite(settings.ssr_thickness) && settings.ssr_thickness > 0.0F &&
           settings.ssr_thickness <= 10.0F && std::isfinite(settings.ssr_roughness_cutoff) &&
           settings.ssr_roughness_cutoff >= 0.0F && settings.ssr_roughness_cutoff <= 1.0F;
}

void set_view(bgfx::ViewId view, const char* name, bgfx::FrameBufferHandle target,
              std::uint16_t width, std::uint16_t height)
{
    bgfx::setViewName(view, name);
    bgfx::setViewFrameBuffer(view, target);
    bgfx::setViewRect(view, 0, 0, width, height);
    bgfx::setViewClear(view, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(view, nullptr, nullptr);
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
}
} // namespace

ScreenSpaceReflectionPass::ScreenSpaceReflectionPass()
{
    programs_.fill(BGFX_INVALID_HANDLE);
    samplers_.fill(BGFX_INVALID_HANDLE);
    uniforms_.fill(BGFX_INVALID_HANDLE);
    hierarchy_.fill(BGFX_INVALID_HANDLE);
    hierarchy_frames_.fill(BGFX_INVALID_HANDLE);
}

ScreenSpaceReflectionPass::~ScreenSpaceReflectionPass() noexcept
{
    release();
}

bool ScreenSpaceReflectionPass::prepare(std::uint16_t width, std::uint16_t height,
                                        const RenderSettings& settings)
{
    if (!settings.ssr_enabled)
    {
        release();
        return true;
    }
    const auto* caps = bgfx::getCaps();
    const auto flags = BGFX_TEXTURE_RT | point_clamp;
    if (!valid_settings(settings) || caps == nullptr || width == 0 || height == 0 ||
        width > caps->limits.maxTextureSize || height > caps->limits.maxTextureSize ||
        caps->limits.maxTextureSamplers < 8 || caps->limits.maxViews <= composite_view ||
        !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::R32F, flags) ||
        !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA16F, flags))
    {
        return false;
    }
    const auto trace_width = static_cast<std::uint16_t>(
        std::max(1.0F, std::ceil(width * settings.ssr_resolution_scale)));
    const auto trace_height = static_cast<std::uint16_t>(
        std::max(1.0F, std::ceil(height * settings.ssr_resolution_scale)));
    if (width_ == width && height_ == height && trace_width_ == trace_width &&
        trace_height_ == trace_height && bgfx::isValid(color_frame_))
    {
        return true;
    }
    release();
    width_ = width;
    height_ = height;
    trace_width_ = trace_width;
    trace_height_ = trace_height;
    struct Bytecode
    {
        const std::uint8_t* data;
        std::uint32_t size;
    };
    const std::array fragments = {Bytecode{fs_ssr_reduce, sizeof(fs_ssr_reduce)},
                                  Bytecode{fs_ssr_trace, sizeof(fs_ssr_trace)},
                                  Bytecode{fs_ssr_composite, sizeof(fs_ssr_composite)}};
    const auto vertex = bgfx::createShader(bgfx::copy(vs_hdr, sizeof(vs_hdr)));
    if (!bgfx::isValid(vertex))
    {
        release();
        return false;
    }
    for (std::size_t index = 0; index < programs_.size(); ++index)
    {
        const auto fragment =
            bgfx::createShader(bgfx::copy(fragments[index].data, fragments[index].size));
        if (bgfx::isValid(fragment))
        {
            programs_[index] = bgfx::createProgram(vertex, fragment, false);
            bgfx::destroy(fragment);
        }
    }
    bgfx::destroy(vertex);
    constexpr std::array sampler_names = {"s_ssr_scene",  "s_ssr_ibl",      "s_ssr_depth",
                                          "s_ssr_normal", "s_ssr_specular", "s_ssr_lut",
                                          "s_ssr_even",   "s_ssr_odd"};
    constexpr std::array uniform_names = {
        "u_ssr_dimensions", "u_ssr_parameters", "u_ssr_frame",      "u_ssr_reduce",
        "u_ssr_target",     "u_ssr_view",       "u_ssr_projection", "u_ssr_inverse_projection"};
    for (std::size_t index = 0; index < samplers_.size(); ++index)
    {
        samplers_[index] = bgfx::createUniform(sampler_names[index], bgfx::UniformType::Sampler);
        uniforms_[index] = bgfx::createUniform(
            uniform_names[index], index < 5 ? bgfx::UniformType::Vec4 : bgfx::UniformType::Mat4);
    }
    struct Vertex
    {
        float x, y, z, u, v;
    };
    std::array vertices = {Vertex{-1, -1, 0, 0, 1}, Vertex{3, -1, 0, 2, 1},
                           Vertex{-1, 3, 0, 0, -1}};
    if (caps->originBottomLeft)
    {
        for (auto& value : vertices)
        {
            value.v = 1.0F - value.v;
        }
    }
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    fullscreen_ = bgfx::createVertexBuffer(bgfx::copy(vertices.data(), sizeof(vertices)), layout);
    for (auto& texture : hierarchy_)
    {
        texture = bgfx::createTexture2D(width, height, true, 1, bgfx::TextureFormat::R32F, flags);
    }
    reflection_ = bgfx::createTexture2D(trace_width, trace_height, false, 1,
                                        bgfx::TextureFormat::RGBA16F, flags);
    color_ = bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::RGBA16F, flags);
    bool valid = bgfx::isValid(fullscreen_) && bgfx::isValid(reflection_) && bgfx::isValid(color_);
    for (auto value : programs_)
    {
        valid = valid && bgfx::isValid(value);
    }
    for (auto value : samplers_)
    {
        valid = valid && bgfx::isValid(value);
    }
    for (auto value : uniforms_)
    {
        valid = valid && bgfx::isValid(value);
    }
    for (auto value : hierarchy_)
    {
        valid = valid && bgfx::isValid(value);
    }
    if (!valid)
    {
        release();
        return false;
    }
    auto mip_width = width;
    auto mip_height = height;
    for (std::uint16_t index = 0; index < hierarchy_frames_.size(); ++index)
    {
        bgfx::Attachment attachment;
        attachment.init(hierarchy_[index % 2], bgfx::Access::Write, 0, 1, index, BGFX_RESOLVE_NONE);
        hierarchy_frames_[index] = bgfx::createFrameBuffer(1, &attachment, false);
        ++levels_;
        if (mip_width == 1 && mip_height == 1)
        {
            break;
        }
        mip_width = std::max<std::uint16_t>(1, mip_width / 2);
        mip_height = std::max<std::uint16_t>(1, mip_height / 2);
    }
    reflection_frame_ = bgfx::createFrameBuffer(1, &reflection_, false);
    color_frame_ = bgfx::createFrameBuffer(1, &color_, false);
    valid = bgfx::isValid(reflection_frame_) && bgfx::isValid(color_frame_);
    for (std::uint16_t index = 0; index < levels_; ++index)
    {
        valid = valid && bgfx::isValid(hierarchy_frames_[index]);
    }
    if (!valid)
    {
        release();
    }
    return valid;
}

bool ScreenSpaceReflectionPass::render(const float* view, const float* projection,
                                       bgfx::TextureHandle scene_color,
                                       bgfx::TextureHandle ibl_specular, bgfx::TextureHandle depth,
                                       bgfx::TextureHandle normal_roughness,
                                       bgfx::TextureHandle specular, bgfx::TextureHandle brdf_lut,
                                       const RenderSettings& settings)
{
    if (!settings.ssr_enabled || !valid_settings(settings) || !bgfx::isValid(color_frame_) ||
        view == nullptr || projection == nullptr || !finite_matrix(view) ||
        !finite_matrix(projection))
    {
        return false;
    }
    // The analytical DFG fallback does not sample this slot, but bgfx still needs a
    // valid texture bound for the statically declared sampler.
    const bool has_lut = bgfx::isValid(brdf_lut);
    const std::array inputs = {scene_color,      ibl_specular, depth,
                               normal_roughness, specular,     has_lut ? brdf_lut : specular};
    for (auto input : inputs)
    {
        if (!bgfx::isValid(input) || input.idx == color_.idx || input.idx == reflection_.idx)
        {
            return false;
        }
    }
    float inverse[16]{};
    bx::mtxInverse(inverse, projection);
    if (!finite_matrix(inverse))
    {
        return false;
    }
    const auto* caps = bgfx::getCaps();
    const float dimensions[] = {static_cast<float>(width_), static_cast<float>(height_),
                                static_cast<float>(trace_width_),
                                static_cast<float>(trace_height_)};
    const float parameters[] = {static_cast<float>(settings.ssr_max_steps),
                                settings.ssr_max_distance, settings.ssr_thickness,
                                settings.ssr_roughness_cutoff};
    const float frame[] = {caps->originBottomLeft ? 1.0F : 0.0F,
                           caps->homogeneousDepth ? 1.0F : 0.0F, static_cast<float>(levels_),
                           std::abs(projection[15]) > 0.5F ? 1.0F : 0.0F};
    const auto bind_common = [&]()
    {
        const float target[]{0, 0, has_lut ? 1.0F : 0.0F, 0};
        bgfx::setUniform(uniforms_[0], dimensions);
        bgfx::setUniform(uniforms_[1], parameters);
        bgfx::setUniform(uniforms_[2], frame);
        bgfx::setUniform(uniforms_[4], target);
        bgfx::setUniform(uniforms_[5], view);
        bgfx::setUniform(uniforms_[6], projection);
        bgfx::setUniform(uniforms_[7], inverse);
        bgfx::setVertexBuffer(0, fullscreen_);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    };
    for (std::uint16_t level = 0; level < levels_; ++level)
    {
        const auto mip_width = std::max<std::uint16_t>(1, width_ >> level);
        const auto mip_height = std::max<std::uint16_t>(1, height_ >> level);
        const auto previous_width =
            std::max<std::uint16_t>(1, width_ >> (level == 0 ? 0 : level - 1));
        const auto previous_height =
            std::max<std::uint16_t>(1, height_ >> (level == 0 ? 0 : level - 1));
        const float reduce[] = {
            static_cast<float>(previous_width), static_cast<float>(previous_height),
            static_cast<float>(level == 0 ? 0 : level - 1), level == 0 ? 1.0F : 0.0F};
        const float target[] = {static_cast<float>(mip_width), static_cast<float>(mip_height), 0,
                                0};
        const auto depth_view = static_cast<bgfx::ViewId>(first_depth_view + level);
        set_view(depth_view, "SSR depth hierarchy", hierarchy_frames_[level], mip_width,
                 mip_height);
        bind_common();
        bgfx::setUniform(uniforms_[3], reduce);
        bgfx::setUniform(uniforms_[4], target);
        bgfx::setTexture(2, samplers_[2], level == 0 ? depth : hierarchy_[(level - 1) % 2],
                         point_clamp);
        bgfx::submit(depth_view, programs_[0]);
    }
    set_view(trace_view, "SSR trace", reflection_frame_, trace_width_, trace_height_);
    bind_common();
    for (std::uint8_t index = 0; index < inputs.size(); ++index)
    {
        bgfx::setTexture(index, samplers_[index], inputs[index],
                         index == 0 || index == 5 ? BGFX_SAMPLER_UVW_CLAMP : point_clamp);
    }
    bgfx::setTexture(6, samplers_[6], hierarchy_[0], point_clamp);
    bgfx::setTexture(7, samplers_[7], hierarchy_[1], point_clamp);
    bgfx::submit(trace_view, programs_[1]);
    set_view(composite_view, "SSR composite", color_frame_, width_, height_);
    bind_common();
    bgfx::setTexture(0, samplers_[0], scene_color, point_clamp);
    bgfx::setTexture(1, samplers_[1], ibl_specular, point_clamp);
    bgfx::setTexture(2, samplers_[2], depth, point_clamp);
    bgfx::setTexture(3, samplers_[3], normal_roughness, point_clamp);
    bgfx::setTexture(4, samplers_[4], reflection_, point_clamp);
    bgfx::setTexture(5, samplers_[5], specular, point_clamp);
    bgfx::submit(composite_view, programs_[2]);
    return true;
}

bgfx::TextureHandle ScreenSpaceReflectionPass::texture() const noexcept
{
    return color_;
}

void ScreenSpaceReflectionPass::release() noexcept
{
    for (std::uint16_t index = 0; index < levels_; ++index)
    {
        bgfx::setViewFrameBuffer(first_depth_view + index, BGFX_INVALID_HANDLE);
    }
    if (bgfx::isValid(reflection_frame_))
    {
        bgfx::setViewFrameBuffer(trace_view, BGFX_INVALID_HANDLE);
    }
    if (bgfx::isValid(color_frame_))
    {
        bgfx::setViewFrameBuffer(composite_view, BGFX_INVALID_HANDLE);
    }
    for (auto& value : hierarchy_frames_)
    {
        if (bgfx::isValid(value))
        {
            bgfx::destroy(value);
        }
        value = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(reflection_frame_))
    {
        bgfx::destroy(reflection_frame_);
    }
    if (bgfx::isValid(color_frame_))
    {
        bgfx::destroy(color_frame_);
    }
    if (bgfx::isValid(reflection_))
    {
        bgfx::destroy(reflection_);
    }
    if (bgfx::isValid(color_))
    {
        bgfx::destroy(color_);
    }
    if (bgfx::isValid(fullscreen_))
    {
        bgfx::destroy(fullscreen_);
    }
    for (auto& value : hierarchy_)
    {
        if (bgfx::isValid(value))
        {
            bgfx::destroy(value);
        }
        value = BGFX_INVALID_HANDLE;
    }
    for (auto& value : programs_)
    {
        if (bgfx::isValid(value))
        {
            bgfx::destroy(value);
        }
        value = BGFX_INVALID_HANDLE;
    }
    for (auto& value : samplers_)
    {
        if (bgfx::isValid(value))
        {
            bgfx::destroy(value);
        }
        value = BGFX_INVALID_HANDLE;
    }
    for (auto& value : uniforms_)
    {
        if (bgfx::isValid(value))
        {
            bgfx::destroy(value);
        }
        value = BGFX_INVALID_HANDLE;
    }
    reflection_frame_ = color_frame_ = BGFX_INVALID_HANDLE;
    reflection_ = color_ = BGFX_INVALID_HANDLE;
    fullscreen_ = BGFX_INVALID_HANDLE;
    width_ = height_ = trace_width_ = trace_height_ = levels_ = 0;
}

std::uint32_t ScreenSpaceReflectionPass::resource_count() const noexcept
{
    std::uint32_t count = static_cast<std::uint32_t>(bgfx::isValid(reflection_)) +
                          static_cast<std::uint32_t>(bgfx::isValid(color_)) +
                          static_cast<std::uint32_t>(bgfx::isValid(reflection_frame_)) +
                          static_cast<std::uint32_t>(bgfx::isValid(color_frame_)) +
                          static_cast<std::uint32_t>(bgfx::isValid(fullscreen_));
    for (auto value : hierarchy_)
    {
        count += bgfx::isValid(value);
    }
    for (auto value : hierarchy_frames_)
    {
        count += bgfx::isValid(value);
    }
    for (auto value : programs_)
    {
        count += bgfx::isValid(value);
    }
    for (auto value : samplers_)
    {
        count += bgfx::isValid(value);
    }
    for (auto value : uniforms_)
    {
        count += bgfx::isValid(value);
    }
    return count;
}
} // namespace engine::render_detail
