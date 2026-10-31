#include "runtime/render/ambient_occlusion_pass.hpp"

#include "runtime/render/render_system.hpp"

#include <SDL3/SDL.h>

#include "cs_assao_apply.bin.h"
#include "cs_assao_generate_q2.bin.h"
#include "cs_assao_prepare_depth_mip.bin.h"
#include "cs_assao_prepare_depths_and_normals.bin.h"
#include "cs_assao_smart_blur.bin.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine
{
namespace
{

constexpr bgfx::ViewId depth_view = 4;
constexpr bgfx::ViewId compute_view = 5;
constexpr std::uint16_t uniform_count = 19;
constexpr std::uint32_t point_clamp = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
constexpr std::uint32_t point_mirror = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_MIRROR;
constexpr std::uint32_t linear_clamp = BGFX_SAMPLER_UVW_CLAMP;
constexpr std::uint8_t depth_mip_levels = 4;
constexpr std::size_t prepare_program = 0;
constexpr std::size_t mip_program = 1;
constexpr std::size_t generate_program = 2;
constexpr std::size_t blur_program = 3;
constexpr std::size_t apply_program = 4;
constexpr std::size_t depth_sampler = 0;
constexpr std::size_t view_depth_sampler = 1;
constexpr std::size_t mirror_depth_sampler = 2;
constexpr std::size_t blur_sampler = 3;
constexpr std::size_t final_sampler = 4;

using AoUniforms = std::array<std::array<float, 4>, uniform_count>;

bool make_uniforms(const CameraSettings& camera, const RenderSettings& settings,
                   const float* projection, std::uint16_t width, std::uint16_t height,
                   AoUniforms& uniforms)
{
    uniforms = {};
    if (projection == nullptr ||
        !std::all_of(projection, projection + 16,
                     [](float value) { return std::isfinite(value); }) ||
        !(projection[0] > 0.0F) || !(projection[5] > 0.0F) || !std::isfinite(camera.near_clip) ||
        !std::isfinite(camera.far_clip) || camera.near_clip <= 0.0F ||
        camera.far_clip <= camera.near_clip ||
        (camera.projection != schema::Projection::perspective &&
         camera.projection != schema::Projection::orthographic) ||
        !std::isfinite(settings.ao_radius) || settings.ao_radius <= 0.0F ||
        settings.ao_radius > 100.0F || !std::isfinite(settings.ao_strength) ||
        settings.ao_strength < 0.0F || settings.ao_strength > 2.0F ||
        !std::isfinite(settings.ao_power) || settings.ao_power < 0.1F || settings.ao_power > 8.0F ||
        !std::isfinite(settings.ao_fade_start) || !std::isfinite(settings.ao_fade_end) ||
        settings.ao_fade_start < 0.0F || settings.ao_fade_end <= settings.ao_fade_start ||
        settings.ao_fade_end > 100000.0F)
    {
        return false;
    }
    const float inverse_width = 1.0F / width;
    const float inverse_height = 1.0F / height;
    uniforms[0] = {inverse_width, inverse_height, inverse_width * 2.0F, inverse_height * 2.0F};
    const float clip_range = camera.far_clip - camera.near_clip;
    const bool orthographic = camera.projection == schema::Projection::orthographic;
    if (orthographic)
    {
        uniforms[1] = {clip_range, camera.near_clip, 1.0F, 0.0F};
    }
    else
    {
        uniforms[1] = {camera.far_clip * camera.near_clip / clip_range,
                       camera.far_clip / clip_range, 0.0F, 0.0F};
    }
    const float projected_half_width = 1.0F / projection[0];
    const float projected_half_height = 1.0F / projection[5];
    const float y_direction = bgfx::getCaps()->originBottomLeft ? 1.0F : -1.0F;
    uniforms[2] = {projected_half_width * 2.0F, projected_half_height * 2.0F * y_direction,
                   -projected_half_width, -projected_half_height * y_direction};
    uniforms[4] = {inverse_width * 2.0F, inverse_height * 2.0F, inverse_width * 0.5F,
                   inverse_height * 0.5F};
    uniforms[5] = {settings.ao_radius, settings.ao_strength * 4.3F, settings.ao_power, 0.98F};
    const float fade_range = settings.ao_fade_end - settings.ao_fade_start;
    const float sampling_near_limit = settings.ao_radius * 1.2F / projected_half_height;
    uniforms[6] = {-1.0F / fade_range, settings.ao_fade_end / fade_range, 0.06F,
                   1.0F / sampling_near_limit};
    uniforms[7] = {0.9992F, -1.0F / settings.ao_radius, 0.0F, 0.0F};
    uniforms[8] = {0.02F, 0.0F, inverse_width * 4.0F, inverse_height * 4.0F};
    uniforms[14] = {2.0F, -1.0F, 0.5F, 0.0F};
    for (std::size_t index = 0; index < 4; ++index)
    {
        uniforms[15 + index][index] = 1.0F;
    }
    return std::all_of(uniforms.begin(), uniforms.end(),
                       [](const auto& vector)
                       {
                           return std::all_of(vector.begin(), vector.end(),
                                              [](float value) { return std::isfinite(value); });
                       });
}

void set_phase(AoUniforms& uniforms, std::uint16_t pass, bool origin_bottom_left)
{
    const float x = static_cast<float>(pass % 2);
    const float y =
        origin_bottom_left ? 1.0F - static_cast<float>(pass / 2) : static_cast<float>(pass / 2);
    uniforms[3] = {x, y, x * uniforms[0][0], y * uniforms[0][1]};
    uniforms[8][1] = static_cast<float>(pass);
    constexpr std::array order = {0, 1, 4, 3, 2};
    constexpr float pi = 3.14159265358979323846F;
    for (std::size_t index = 0; index < order.size(); ++index)
    {
        const float subpass = static_cast<float>(order[index]);
        const float angle = (static_cast<float>(pass) + subpass / 5.0F) * pi * 0.5F;
        const float scale =
            1.0F + (static_cast<float>(pass) - 1.5F + (subpass - 2.0F) / 5.0F) * 0.07F;
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        uniforms[9 + index] = {scale * cosine, -scale * sine, -scale * sine, -scale * cosine};
    }
}

bool supports_ao(const bgfx::Caps& caps)
{
    constexpr auto required = BGFX_CAPS_COMPUTE | BGFX_CAPS_TEXTURE_2D_ARRAY;
    if ((caps.supported & required) != required || caps.limits.maxComputeBindings < 8 ||
        caps.limits.maxTextureLayers < 4)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "ASSAO device limits: compute=%u arrays=%u bindings=%u layers=%u",
                     (caps.supported & BGFX_CAPS_COMPUTE) != 0 ? 1U : 0U,
                     (caps.supported & BGFX_CAPS_TEXTURE_2D_ARRAY) != 0 ? 1U : 0U,
                     caps.limits.maxComputeBindings, caps.limits.maxTextureLayers);
        return false;
    }
    constexpr auto depth_flags = BGFX_TEXTURE_RT | point_clamp;
    constexpr auto depth_capabilities =
        BGFX_CAPS_FORMAT_TEXTURE_2D | BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER;
    if ((caps.formats[bgfx::TextureFormat::D32F] & depth_capabilities) != depth_capabilities ||
        !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D32F, depth_flags))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "ASSAO sampled D32F render target is unsupported (format caps=0x%x)",
                     static_cast<unsigned int>(caps.formats[bgfx::TextureFormat::D32F]));
        return false;
    }
    // bgfx Metal reports IMAGE_READ/WRITE from MTLReadWriteTextureTier, which describes
    // access::read_write in one shader. ASSAO uses only separate read/write bindings and
    // these four standard Metal formats support them even when read_write is unavailable
    // (in particular RG8 has TierNone in upstream renderer_mtl.mm). Other backends expose
    // their independent image capabilities through these flags and must retain the check.
    const bool metal_separate_access = caps.rendererType == bgfx::RendererType::Metal;
    for (auto format : {bgfx::TextureFormat::R16F, bgfx::TextureFormat::RGBA8,
                        bgfx::TextureFormat::RG8, bgfx::TextureFormat::R8})
    {
        const auto image_capabilities =
            BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE |
            (format == bgfx::TextureFormat::R16F || format == bgfx::TextureFormat::RGBA8
                 ? BGFX_CAPS_FORMAT_TEXTURE_IMAGE_READ
                 : 0);
        const auto required_format =
            BGFX_CAPS_FORMAT_TEXTURE_2D | (metal_separate_access ? 0U : image_capabilities);
        const auto layers = static_cast<std::uint16_t>(format == bgfx::TextureFormat::RG8 ? 4 : 1);
        if ((caps.formats[format] & required_format) != required_format ||
            !bgfx::isTextureValid(0, false, layers, format, BGFX_TEXTURE_COMPUTE_WRITE))
        {
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER,
                "ASSAO compute texture is unsupported (format=%u caps=0x%x required=0x%x "
                "layers=%u)",
                static_cast<unsigned int>(format), static_cast<unsigned int>(caps.formats[format]),
                static_cast<unsigned int>(required_format), static_cast<unsigned int>(layers));
            return false;
        }
    }
    return true;
}

} // namespace

AmbientOcclusionPass::~AmbientOcclusionPass() noexcept
{
    release();
}

bool AmbientOcclusionPass::initialize_programs()
{
    if (std::all_of(programs_.begin(), programs_.end(),
                    [](auto handle) { return bgfx::isValid(handle); }))
    {
        return true;
    }
    release();
    // bgfx 39-assao, 7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099, Q2 + two smart blurs.
    // Copyright 2018 Attila Kocsis; BSD-2-Clause, third_party/notices/bgfx-assao.md.
    struct Bytecode
    {
        const std::uint8_t* data;
        std::uint32_t size;
    };
    const std::array bytecode = {
        Bytecode{cs_assao_prepare_depths_and_normals,
                 static_cast<std::uint32_t>(sizeof(cs_assao_prepare_depths_and_normals))},
        Bytecode{cs_assao_prepare_depth_mip,
                 static_cast<std::uint32_t>(sizeof(cs_assao_prepare_depth_mip))},
        Bytecode{cs_assao_generate_q2, static_cast<std::uint32_t>(sizeof(cs_assao_generate_q2))},
        Bytecode{cs_assao_smart_blur, static_cast<std::uint32_t>(sizeof(cs_assao_smart_blur))},
        Bytecode{cs_assao_apply, static_cast<std::uint32_t>(sizeof(cs_assao_apply))}};
    for (std::size_t index = 0; index < programs_.size(); ++index)
    {
        const auto shader =
            bgfx::createShader(bgfx::copy(bytecode[index].data, bytecode[index].size));
        if (!bgfx::isValid(shader))
        {
            return false;
        }
        programs_[index] = bgfx::createProgram(shader, false);
        bgfx::destroy(shader);
        if (!bgfx::isValid(programs_[index]))
        {
            return false;
        }
    }
    constexpr std::array sampler_names = {"s_depthSource", "s_viewspaceDepthSource",
                                          "s_viewspaceDepthSourceMirror", "s_blurInput",
                                          "s_finalSSAO"};
    for (std::size_t index = 0; index < samplers_.size(); ++index)
    {
        samplers_[index] = bgfx::createUniform(sampler_names[index], bgfx::UniformType::Sampler);
        if (!bgfx::isValid(samplers_[index]))
        {
            return false;
        }
    }
    parameters_ = bgfx::createUniform("u_assao_params", bgfx::UniformType::Vec4, uniform_count);
    rectangle_ = bgfx::createUniform("u_assao_rect", bgfx::UniformType::Vec4);
    return bgfx::isValid(parameters_) && bgfx::isValid(rectangle_);
}

bool AmbientOcclusionPass::prepare(std::uint16_t width, std::uint16_t height)
{
    const auto* caps = bgfx::getCaps();
    const auto aligned_width = std::max(16U, (static_cast<std::uint32_t>(width) + 15U) & ~15U);
    const auto aligned_height = std::max(16U, (static_cast<std::uint32_t>(height) + 15U) & ~15U);
    if (caps == nullptr || width == 0 || height == 0 || !supports_ao(*caps) ||
        aligned_width > caps->limits.maxTextureSize ||
        aligned_height > caps->limits.maxTextureSize ||
        aligned_width > std::numeric_limits<std::uint16_t>::max() ||
        aligned_height > std::numeric_limits<std::uint16_t>::max())
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "ASSAO requires compute, sampled D32F, typed images and texture arrays");
        release();
        return false;
    }
    if (!initialize_programs())
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot initialize ASSAO compute programs");
        release();
        return false;
    }
    if (width_ == aligned_width && height_ == aligned_height && bgfx::isValid(depth_framebuffer_))
    {
        return true;
    }
    release_textures();
    width_ = static_cast<std::uint16_t>(aligned_width);
    height_ = static_cast<std::uint16_t>(aligned_height);
    const auto half_width = static_cast<std::uint16_t>(width_ / 2);
    const auto half_height = static_cast<std::uint16_t>(height_ / 2);
    depth_ = bgfx::createTexture2D(width_, height_, false, 1, bgfx::TextureFormat::D32F,
                                   BGFX_TEXTURE_RT | point_clamp);
    if (bgfx::isValid(depth_))
    {
        depth_framebuffer_ = bgfx::createFrameBuffer(1, &depth_, false);
    }
    normals_ = bgfx::createTexture2D(width_, height_, false, 1, bgfx::TextureFormat::RGBA8,
                                     BGFX_TEXTURE_COMPUTE_WRITE);
    for (auto& handle : half_depths_)
    {
        handle = bgfx::createTexture2D(half_width, half_height, true, 1, bgfx::TextureFormat::R16F,
                                       BGFX_TEXTURE_COMPUTE_WRITE | point_clamp);
    }
    for (auto& handle : ping_pong_)
    {
        // Two layers ensure an array texture; the transient ping-pong data uses layer zero.
        handle = bgfx::createTexture2D(half_width, half_height, false, 2, bgfx::TextureFormat::RG8,
                                       BGFX_TEXTURE_COMPUTE_WRITE);
    }
    final_results_ =
        bgfx::createTexture2D(half_width, half_height, false, 4, bgfx::TextureFormat::RG8,
                              BGFX_TEXTURE_COMPUTE_WRITE | linear_clamp);
    ao_ = bgfx::createTexture2D(width_, height_, false, 1, bgfx::TextureFormat::R8,
                                BGFX_TEXTURE_COMPUTE_WRITE | point_clamp);
    if (!bgfx::isValid(depth_framebuffer_) || !bgfx::isValid(normals_) ||
        !bgfx::isValid(final_results_) || !bgfx::isValid(ao_) ||
        !std::all_of(half_depths_.begin(), half_depths_.end(),
                     [](auto handle) { return bgfx::isValid(handle); }) ||
        !std::all_of(ping_pong_.begin(), ping_pong_.end(),
                     [](auto handle) { return bgfx::isValid(handle); }))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot allocate ASSAO frame textures");
        release();
        return false;
    }
    return true;
}

void AmbientOcclusionPass::begin_depth(const float* view, const float* projection)
{
    bgfx::setViewName(depth_view, "ASSAO camera depth");
    bgfx::setViewFrameBuffer(depth_view, depth_framebuffer_);
    bgfx::setViewRect(depth_view, 0, 0, width_, height_);
    bgfx::setViewClear(depth_view, BGFX_CLEAR_DEPTH, 0, 1.0F, 0);
    bgfx::setViewTransform(depth_view, view, projection);
    bgfx::touch(depth_view);
}

bool AmbientOcclusionPass::compute(const CameraSettings& camera, const RenderSettings& settings,
                                   const float* projection)
{
    AoUniforms uniforms;
    if (width_ == 0 || height_ == 0 || !bgfx::isValid(ao_) ||
        !make_uniforms(camera, settings, projection, width_, height_, uniforms))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "ASSAO projection or settings are invalid");
        return false;
    }
    const auto half_width = static_cast<std::uint16_t>(width_ / 2);
    const auto half_height = static_cast<std::uint16_t>(height_ / 2);
    const std::uint32_t half_groups_x = (half_width + 7U) / 8U;
    const std::uint32_t half_groups_y = (half_height + 7U) / 8U;
    const float half_rect[] = {0.0F, 0.0F, static_cast<float>(half_width),
                               static_cast<float>(half_height)};
    bgfx::setViewName(compute_view, "ASSAO Q2");
    bgfx::setViewFrameBuffer(compute_view, BGFX_INVALID_HANDLE);
    bgfx::setViewMode(compute_view, bgfx::ViewMode::Sequential);
    bgfx::setUniform(parameters_, uniforms.data(), uniform_count);
    bgfx::setTexture(0, samplers_[depth_sampler], depth_, point_clamp);
    for (std::uint8_t index = 0; index < half_depths_.size(); ++index)
    {
        bgfx::setImage(static_cast<std::uint8_t>(index + 1), half_depths_[index], 0,
                       bgfx::Access::Write, bgfx::TextureFormat::R16F);
    }
    bgfx::setImage(5, normals_, 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA8);
    bgfx::dispatch(compute_view, programs_[prepare_program], half_groups_x, half_groups_y);

    for (std::uint8_t mip = 1; mip < depth_mip_levels; ++mip)
    {
        const auto mip_width = static_cast<std::uint16_t>(half_width >> mip);
        const auto mip_height = static_cast<std::uint16_t>(half_height >> mip);
        for (std::uint8_t index = 0; index < half_depths_.size(); ++index)
        {
            bgfx::setImage(index, half_depths_[index], static_cast<std::uint8_t>(mip - 1),
                           bgfx::Access::Read, bgfx::TextureFormat::R16F);
            bgfx::setImage(static_cast<std::uint8_t>(index + 4), half_depths_[index], mip,
                           bgfx::Access::Write, bgfx::TextureFormat::R16F);
        }
        const float rect[] = {0.0F, 0.0F, static_cast<float>(mip_width),
                              static_cast<float>(mip_height)};
        bgfx::setUniform(parameters_, uniforms.data(), uniform_count);
        bgfx::setUniform(rectangle_, rect);
        bgfx::dispatch(compute_view, programs_[mip_program], (mip_width + 7U) / 8U,
                       (mip_height + 7U) / 8U);
    }

    // bgfx compute sort keys preserve submission order within the view. Each phase consumes
    // its prepared depth and writes its own final layer, sharing the two transient blur arrays.
    for (std::uint16_t pass = 0; pass < 4; ++pass)
    {
        set_phase(uniforms, pass, bgfx::getCaps()->originBottomLeft);
        uniforms[14][3] = 0.0F;
        bgfx::setUniform(parameters_, uniforms.data(), uniform_count);
        bgfx::setUniform(rectangle_, half_rect);
        bgfx::setTexture(0, samplers_[view_depth_sampler], half_depths_[pass], point_clamp);
        bgfx::setTexture(1, samplers_[mirror_depth_sampler], half_depths_[pass], point_mirror);
        bgfx::setImage(2, normals_, 0, bgfx::Access::Read, bgfx::TextureFormat::RGBA8);
        bgfx::setImage(6, ping_pong_[0], 0, bgfx::Access::Write, bgfx::TextureFormat::RG8);
        bgfx::dispatch(compute_view, programs_[generate_program], half_groups_x, half_groups_y);

        for (std::size_t blur = 0; blur < 2; ++blur)
        {
            uniforms[14][3] = blur == 0 ? 0.0F : static_cast<float>(pass);
            bgfx::setUniform(parameters_, uniforms.data(), uniform_count);
            bgfx::setUniform(rectangle_, half_rect);
            bgfx::setTexture(1, samplers_[blur_sampler], ping_pong_[blur], point_mirror);
            bgfx::setImage(0, blur == 0 ? ping_pong_[1] : final_results_, 0, bgfx::Access::Write,
                           bgfx::TextureFormat::RG8);
            bgfx::dispatch(compute_view, programs_[blur_program], half_groups_x, half_groups_y);
        }
    }
    const float full_rect[] = {0.0F, 0.0F, static_cast<float>(width_), static_cast<float>(height_)};
    bgfx::setUniform(parameters_, uniforms.data(), uniform_count);
    bgfx::setUniform(rectangle_, full_rect);
    bgfx::setTexture(1, samplers_[final_sampler], final_results_, linear_clamp);
    bgfx::setImage(0, ao_, 0, bgfx::Access::Write, bgfx::TextureFormat::R8);
    bgfx::dispatch(compute_view, programs_[apply_program], (width_ + 7U) / 8U, (height_ + 7U) / 8U);
    return true;
}

bgfx::TextureHandle AmbientOcclusionPass::texture() const noexcept
{
    return ao_;
}

std::uint32_t AmbientOcclusionPass::resource_count() const noexcept
{
    std::uint32_t count = static_cast<std::uint32_t>(bgfx::isValid(parameters_)) +
                          static_cast<std::uint32_t>(bgfx::isValid(rectangle_)) +
                          static_cast<std::uint32_t>(bgfx::isValid(depth_framebuffer_)) +
                          static_cast<std::uint32_t>(bgfx::isValid(depth_)) +
                          static_cast<std::uint32_t>(bgfx::isValid(normals_)) +
                          static_cast<std::uint32_t>(bgfx::isValid(final_results_)) +
                          static_cast<std::uint32_t>(bgfx::isValid(ao_));
    for (auto handle : programs_)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(handle));
    }
    for (auto handle : samplers_)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(handle));
    }
    for (auto handle : half_depths_)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(handle));
    }
    for (auto handle : ping_pong_)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(handle));
    }
    return count;
}

void AmbientOcclusionPass::release_textures() noexcept
{
    if (bgfx::isValid(depth_framebuffer_))
    {
        bgfx::setViewFrameBuffer(depth_view, BGFX_INVALID_HANDLE);
        bgfx::destroy(depth_framebuffer_);
        depth_framebuffer_ = BGFX_INVALID_HANDLE;
    }
    for (auto* handle : {&depth_, &normals_, &final_results_, &ao_})
    {
        if (bgfx::isValid(*handle))
        {
            bgfx::destroy(*handle);
            *handle = BGFX_INVALID_HANDLE;
        }
    }
    for (auto& handle : half_depths_)
    {
        if (bgfx::isValid(handle))
        {
            bgfx::destroy(handle);
            handle = BGFX_INVALID_HANDLE;
        }
    }
    for (auto& handle : ping_pong_)
    {
        if (bgfx::isValid(handle))
        {
            bgfx::destroy(handle);
            handle = BGFX_INVALID_HANDLE;
        }
    }
    width_ = 0;
    height_ = 0;
}

void AmbientOcclusionPass::release() noexcept
{
    release_textures();
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
    for (auto* handle : {&parameters_, &rectangle_})
    {
        if (bgfx::isValid(*handle))
        {
            bgfx::destroy(*handle);
            *handle = BGFX_INVALID_HANDLE;
        }
    }
}

} // namespace engine
