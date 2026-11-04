#include "runtime/render/passes/atmosphere_pass.hpp"

#include "runtime/render/render_constants.hpp"
#include "runtime/render/render_settings.hpp"
#include "runtime/render/scene_draw.hpp"

#include "cs_atmo_multiscatter.bin.h"
#include "cs_atmo_skyview.bin.h"
#include "cs_atmo_transmittance.bin.h"
#include "fs_atmosphere.bin.h"
#include "vs_atmosphere.bin.h"

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <bx/math.h>
#include <cmath>

namespace engine::render_detail
{
namespace
{
constexpr std::uint64_t lut_flags =
    BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
enum Uniform : std::size_t
{
    rayleigh,
    mie,
    ozone,
    planet,
    ground,
    radiance,
    camera,
    sky,
    sun_direction,
    inverse,
    clip
};

bgfx::ProgramHandle compute_program(const std::uint8_t* bytes, std::uint32_t size)
{
    const auto shader = bgfx::createShader(bgfx::copy(bytes, size));
    return bgfx::isValid(shader) ? bgfx::createProgram(shader, true)
                                 : bgfx::ProgramHandle{bgfx::kInvalidHandle};
}
} // namespace

AtmospherePass::~AtmospherePass() noexcept
{
    release();
}

bool AtmospherePass::supported() noexcept
{
    const auto* caps = bgfx::getCaps();
    return caps && (caps->supported & BGFX_CAPS_COMPUTE) &&
           caps->limits.maxViews > atmosphere_capture_skyview &&
           bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA16F, lut_flags);
}

bool AtmospherePass::initialize()
{
    if (bgfx::isValid(programs_[0]))
    {
        return true;
    }
    if (!supported())
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "Atmosphere requires compute and writable RGBA16F LUTs");
        return false;
    }
    programs_[0] = compute_program(cs_atmo_transmittance, sizeof(cs_atmo_transmittance));
    programs_[1] = compute_program(cs_atmo_multiscatter, sizeof(cs_atmo_multiscatter));
    programs_[2] = compute_program(cs_atmo_skyview, sizeof(cs_atmo_skyview));
    const auto vertex = bgfx::createShader(bgfx::copy(vs_atmosphere, sizeof(vs_atmosphere)));
    const auto fragment = bgfx::createShader(bgfx::copy(fs_atmosphere, sizeof(fs_atmosphere)));
    if (bgfx::isValid(vertex) && bgfx::isValid(fragment))
    {
        programs_[3] = bgfx::createProgram(vertex, fragment, false);
    }
    if (bgfx::isValid(vertex))
    {
        bgfx::destroy(vertex);
    }
    if (bgfx::isValid(fragment))
    {
        bgfx::destroy(fragment);
    }
    constexpr const char* names[] = {
        "u_atmosphere_rayleigh", "u_atmosphere_mie",    "u_atmosphere_ozone",
        "u_atmosphere_planet",   "u_atmosphere_ground", "u_atmosphere_radiance",
        "u_atmosphere_view",     "u_atmosphere_sky",    "u_atmosphere_sun_direction",
        "u_atmosphere_inverse",  "u_atmosphere_clip"};
    for (std::size_t index = 0; index < uniforms_.size(); ++index)
    {
        uniforms_[index] = bgfx::createUniform(
            names[index], index == inverse ? bgfx::UniformType::Mat4 : bgfx::UniformType::Vec4);
    }
    constexpr const char* sampler_names[] = {"s_atmosphere_transmittance",
                                             "s_atmosphere_multiscatter", "s_atmosphere_skyview"};
    for (std::size_t index = 0; index < samplers_.size(); ++index)
    {
        samplers_[index] = bgfx::createUniform(sampler_names[index], bgfx::UniformType::Sampler);
    }
    medium_[0] = bgfx::createTexture2D(256, 64, false, 1, bgfx::TextureFormat::RGBA16F, lut_flags);
    medium_[1] = bgfx::createTexture2D(32, 32, false, 1, bgfx::TextureFormat::RGBA16F, lut_flags);
    constexpr float vertices[]{-1, -1, 1, 3, -1, 1, -1, 3, 1};
    bgfx::VertexLayout layout;
    layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    fullscreen_ = bgfx::createVertexBuffer(bgfx::copy(vertices, sizeof(vertices)), layout);
    const auto valid = [](auto handle) { return bgfx::isValid(handle); };
    if (!std::all_of(programs_.begin(), programs_.end(), valid) ||
        !std::all_of(uniforms_.begin(), uniforms_.end(), valid) ||
        !std::all_of(samplers_.begin(), samplers_.end(), valid) ||
        !std::all_of(medium_.begin(), medium_.end(), valid) || !bgfx::isValid(fullscreen_))
    {
        release();
        return false;
    }
    return true;
}

void AtmospherePass::set_medium() const
{
    for (std::size_t index = 0; index < physical_.size(); ++index)
    {
        bgfx::setUniform(uniforms_[index], physical_[index].data());
    }
}

bool AtmospherePass::draw(std::uint64_t scene_id, const RenderSettings& settings,
                          const SceneDrawView& view, std::size_t view_index,
                          bgfx::ViewId skyview_compute)
{
    const auto& data = settings.atmosphere;
    if (scene_id_ != scene_id || !data.atmosphere_enabled || !settings.atmosphere_sun_valid)
    {
        release();
    }
    if (!data.atmosphere_enabled || !settings.atmosphere_sun_valid)
    {
        return true;
    }
    if (!bgfx::isValid(programs_[0]) && !supported())
    {
        // Keep the saved request and the rest of the scene usable. Capability queries report
        // unsupported; this is not an allocation/program failure and must not stop the host.
        return true;
    }
    if (view_index >= skyview_.size() || !view.world || !view.view || !view.projection ||
        !initialize())
    {
        return false;
    }
    scene_id_ = scene_id;
    if (!bgfx::isValid(skyview_[view_index]))
    {
        // Longitude wraps; latitude clamps. Each mirror/capture has its own altitude-dependent LUT.
        skyview_[view_index] =
            bgfx::createTexture2D(192, 108, false, 1, bgfx::TextureFormat::RGBA16F,
                                  BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_V_CLAMP);
        if (!bgfx::isValid(skyview_[view_index]))
        {
            return false;
        }
    }
    const std::array<std::array<float, 4>, 5> parameters = {
        {{static_cast<float>(data.atmosphere_rayleigh_scattering.x()),
          static_cast<float>(data.atmosphere_rayleigh_scattering.y()),
          static_cast<float>(data.atmosphere_rayleigh_scattering.z()),
          data.atmosphere_rayleigh_scale_height},
         {data.atmosphere_mie_scattering,
          data.atmosphere_mie_scattering + data.atmosphere_mie_absorption,
          data.atmosphere_mie_scale_height, data.atmosphere_mie_anisotropy},
         {static_cast<float>(data.atmosphere_ozone_absorption.x()),
          static_cast<float>(data.atmosphere_ozone_absorption.y()),
          static_cast<float>(data.atmosphere_ozone_absorption.z()),
          data.atmosphere_multiscatter_strength},
         {data.atmosphere_planet_radius, data.atmosphere_planet_radius + data.atmosphere_height,
          data.atmosphere_ozone_center_height, data.atmosphere_ozone_half_width},
         {static_cast<float>(data.atmosphere_ground_albedo.x()),
          static_cast<float>(data.atmosphere_ground_albedo.y()),
          static_cast<float>(data.atmosphere_ground_albedo.z()), 0}}};
    if (!medium_valid_ || physical_ != parameters)
    {
        physical_ = parameters;
        set_medium();
        bgfx::setViewName(atmosphere_transmittance_view, "Atmosphere transmittance");
        bgfx::setImage(0, medium_[0], 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
        bgfx::dispatch(atmosphere_transmittance_view, programs_[0], 32, 8);
        set_medium();
        bgfx::setViewName(atmosphere_multiscatter_view, "Atmosphere multiple scattering");
        bgfx::setTexture(0, samplers_[0], medium_[0]);
        bgfx::setImage(1, medium_[1], 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
        bgfx::dispatch(atmosphere_multiscatter_view, programs_[1], 4, 4);
        medium_valid_ = true;
    }
    // The upstream model is near-ground, Y-up. Engine world distances are metres, LUT distances km.
    const float altitude = std::clamp((view.world[13] - data.atmosphere_ground_height) * 0.001F,
                                      0.001F, data.atmosphere_height - 0.001F);
    const float radius =
        std::max(data.atmosphere_planet_radius + altitude,
                 std::nextafter(data.atmosphere_planet_radius,
                                data.atmosphere_planet_radius + data.atmosphere_height));
    const float lighting[]{settings.atmosphere_sun_radiance[0], settings.atmosphere_sun_radiance[1],
                           settings.atmosphere_sun_radiance[2], 0};
    const float view_params[]{-settings.atmosphere_sun_direction[1], radius, 192, 108};
    set_medium();
    bgfx::setUniform(uniforms_[radiance], lighting);
    bgfx::setUniform(uniforms_[camera], view_params);
    bgfx::setTexture(0, samplers_[0], medium_[0]);
    bgfx::setTexture(2, samplers_[1], medium_[1]);
    bgfx::setImage(1, skyview_[view_index], 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bgfx::setViewName(skyview_compute, "Atmosphere sky-view LUT");
    bgfx::dispatch(skyview_compute, programs_[2], 24, 14);

    float combined[16]{}, inverted[16]{};
    bx::mtxMul(combined, view.view, view.projection);
    bx::mtxInverse(inverted, combined);
    constexpr float disc_cosine = 0.9999F, disc_scale = 60.0F; // bgfx 53-sky2 solar disc.
    const float sky_params[]{radius, disc_cosine, disc_scale, 0};
    const float direction[]{settings.atmosphere_sun_direction[0],
                            settings.atmosphere_sun_direction[1],
                            settings.atmosphere_sun_direction[2], 0};
    // Physical sky participates in the same display exposure/tonemap as lit PBR surfaces.
    const float clipping[]{bgfx::getCaps()->homogeneousDepth ? -1.0F : 0.0F, 1, 0, 0};
    set_medium();
    bgfx::setUniform(uniforms_[inverse], inverted);
    bgfx::setUniform(uniforms_[sky], sky_params);
    bgfx::setUniform(uniforms_[sun_direction], direction);
    bgfx::setUniform(uniforms_[radiance], lighting);
    bgfx::setUniform(uniforms_[clip], clipping);
    bgfx::setTexture(0, samplers_[2], skyview_[view_index]);
    bgfx::setTexture(1, samplers_[0], medium_[0]);
    bgfx::setVertexBuffer(0, fullscreen_);
    // Far-depth testing makes the background independent of opaque program sorting. No depth
    // writes.
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL |
                   (view.multisampling ? BGFX_STATE_MSAA : 0));
    bgfx::submit(view.id, programs_[3]);
    if (view_index == 2)
    {
        submitted_scene_id_ = scene_id;
    }
    return true;
}

bool AtmospherePass::submitted_for(std::uint64_t scene_id) const noexcept
{
    return scene_id != 0 && scene_id == submitted_scene_id_;
}

void AtmospherePass::release() noexcept
{
    for (auto& handle : programs_)
    {
        if (bgfx::isValid(handle))
        {
            bgfx::destroy(handle);
        }
        handle = BGFX_INVALID_HANDLE;
    }
    for (auto& handle : uniforms_)
    {
        if (bgfx::isValid(handle))
        {
            bgfx::destroy(handle);
        }
        handle = BGFX_INVALID_HANDLE;
    }
    for (auto& handle : samplers_)
    {
        if (bgfx::isValid(handle))
        {
            bgfx::destroy(handle);
        }
        handle = BGFX_INVALID_HANDLE;
    }
    for (auto& handle : medium_)
    {
        if (bgfx::isValid(handle))
        {
            bgfx::destroy(handle);
        }
        handle = BGFX_INVALID_HANDLE;
    }
    for (auto& handle : skyview_)
    {
        if (bgfx::isValid(handle))
        {
            bgfx::destroy(handle);
        }
        handle = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(fullscreen_))
    {
        bgfx::destroy(fullscreen_);
    }
    fullscreen_ = BGFX_INVALID_HANDLE;
    scene_id_ = 0;
    submitted_scene_id_ = 0;
    medium_valid_ = false;
    physical_ = {};
}

std::uint32_t AtmospherePass::resource_count() const noexcept
{
    const auto count = [](const auto& handles)
    {
        return static_cast<std::uint32_t>(std::count_if(
            handles.begin(), handles.end(), [](auto handle) { return bgfx::isValid(handle); }));
    };
    return count(programs_) + count(uniforms_) + count(samplers_) + count(medium_) +
           count(skyview_) + static_cast<std::uint32_t>(bgfx::isValid(fullscreen_));
}
} // namespace engine::render_detail
