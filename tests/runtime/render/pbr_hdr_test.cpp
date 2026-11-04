#include "runtime/engine_config.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <basisu/encoder/basisu_comp.h>
#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "pbr_hdr_test_frag.bin.h"
#include "pbr_hdr_test_vert.bin.h"

namespace
{
bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "PBR HDR test: %s (SDL: %s)\n", message, SDL_GetError());
    }
    return condition;
}

// This isolated RGBA16F test executes the production direct-light shader with
// controlled inputs. It does not claim to read back the full scene render.
bool test_pbr_hdr_direct_light()
{
    struct Resources
    {
        bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
        bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle target = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle readback = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle framebuffer = BGFX_INVALID_HANDLE;
        std::array<bgfx::UniformHandle, 8> uniforms{};
        Resources()
        {
            uniforms.fill(BGFX_INVALID_HANDLE);
        }
        ~Resources()
        {
            bgfx::setViewFrameBuffer(244, BGFX_INVALID_HANDLE);
            for (auto uniform : uniforms)
            {
                if (bgfx::isValid(uniform))
                {
                    bgfx::destroy(uniform);
                }
            }
            if (bgfx::isValid(framebuffer))
            {
                bgfx::destroy(framebuffer);
            }
            if (bgfx::isValid(target))
            {
                bgfx::destroy(target);
            }
            if (bgfx::isValid(readback))
            {
                bgfx::destroy(readback);
            }
            if (bgfx::isValid(vertices))
            {
                bgfx::destroy(vertices);
            }
            if (bgfx::isValid(program))
            {
                bgfx::destroy(program);
            }
        }
    } resources;
    constexpr std::uint16_t width = 4;
    constexpr std::uint16_t draw_view = 244;
    constexpr std::uint16_t copy_view = 245;
    const auto vertex =
        bgfx::createShader(bgfx::copy(pbr_hdr_test_vert, sizeof(pbr_hdr_test_vert)));
    const auto fragment =
        bgfx::createShader(bgfx::copy(pbr_hdr_test_frag, sizeof(pbr_hdr_test_frag)));
    if (!bgfx::isValid(vertex) || !bgfx::isValid(fragment))
    {
        if (bgfx::isValid(vertex))
        {
            bgfx::destroy(vertex);
        }
        if (bgfx::isValid(fragment))
        {
            bgfx::destroy(fragment);
        }
        return expect(false, "create HDR direct-light shaders");
    }
    resources.program = bgfx::createProgram(vertex, fragment, false);
    bgfx::destroy(vertex);
    bgfx::destroy(fragment);
    constexpr std::array<float, 9> positions{-1, -1, 0, 3, -1, 0, -1, 3, 0};
    bgfx::VertexLayout layout;
    layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    resources.vertices =
        bgfx::createVertexBuffer(bgfx::copy(positions.data(), sizeof(positions)), layout);
    resources.target = bgfx::createTexture2D(width, width, false, 1, bgfx::TextureFormat::RGBA16F,
                                             BGFX_TEXTURE_RT);
    resources.readback = bgfx::createTexture2D(width, width, false, 1, bgfx::TextureFormat::RGBA16F,
                                               BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    if (bgfx::isValid(resources.target))
    {
        resources.framebuffer = bgfx::createFrameBuffer(1, &resources.target, false);
    }
    constexpr std::array<const char*, 8> uniform_names{
        "u_test_parameters",  "u_test_base_color", "u_test_light", "u_material",
        "u_specular_options", "u_capture_mode",    "u_specular",   "u_shadow_settings"};
    for (std::size_t i = 0; i < uniform_names.size(); ++i)
    {
        resources.uniforms[i] = bgfx::createUniform(uniform_names[i], bgfx::UniformType::Vec4);
    }
    if (!expect(bgfx::isValid(resources.program) && bgfx::isValid(resources.vertices) &&
                    bgfx::isValid(resources.readback) && bgfx::isValid(resources.framebuffer) &&
                    std::all_of(resources.uniforms.begin(), resources.uniforms.end(),
                                [](auto value) { return bgfx::isValid(value); }),
                "create RGBA16F direct-light test resources"))
    {
        return false;
    }

    struct Case
    {
        float roughness, metallic, intensity, angle, kind, distance, range, spot_cos;
    };
    constexpr std::array cases{
        Case{0.5F, 0, 10, 0, 0, 1, 10, 1},          Case{0.25F, 1, 10, 0, 0, 1, 10, 1},
        Case{0.045F, 0, 10, 0, 0, 1, 10, 1},        Case{0.5F, 0.5F, 100, 0.6F, 1, 2, 10, 1},
        Case{0.5F, 0.5F, 100, 0.6F, 1, 4, 10, 1},   Case{0.5F, 0.5F, 100, 0.6F, 2, 2, 10, 0.75F},
        Case{0.5F, 0.5F, 100, 0.6F, 2, 2, 10, 0.5F}};
    constexpr std::array<float, 4> base{0.8F, 0.4F, 0.2F, 1};
    constexpr std::array<float, 4> zero{0, 0, 0, 0};
    constexpr std::array<float, 4> specular_options{0, 1, 0, 0};
    constexpr double pi = 3.14159265358979323846;
    bgfx::setViewFrameBuffer(draw_view, resources.framebuffer);
    bgfx::setViewRect(draw_view, 0, 0, width, width);
    bgfx::setViewClear(draw_view, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(draw_view, nullptr, nullptr);
    std::array<std::uint16_t, width * width * 4> pixels{};
    for (const auto& item : cases)
    {
        const std::array parameters{item.roughness, item.metallic, item.intensity, item.angle};
        const std::array light{item.kind, item.distance, item.range, item.spot_cos};
        bgfx::setUniform(resources.uniforms[0], parameters.data());
        bgfx::setUniform(resources.uniforms[1], base.data());
        bgfx::setUniform(resources.uniforms[2], light.data());
        bgfx::setUniform(resources.uniforms[3], zero.data());
        bgfx::setUniform(resources.uniforms[4], specular_options.data());
        for (std::size_t i = 5; i < resources.uniforms.size(); ++i)
        {
            bgfx::setUniform(resources.uniforms[i], zero.data());
        }
        bgfx::setVertexBuffer(0, resources.vertices);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(draw_view, resources.program);
        const bgfx::TextureRegion readback{.handle = resources.readback};
        bgfx::blit(copy_view, readback, bgfx::TextureRegion{.handle = resources.target});
        const auto ready = bgfx::read(readback, pixels.data());
        while (bgfx::frame() < ready)
        {
        }
        const double cosine = std::cos(item.angle);
        const double half_cosine = std::cos(item.angle * 0.5);
        const double alpha2 = std::pow(static_cast<double>(item.roughness), 4);
        const double denominator = 1 + (alpha2 - 1) * half_cosine * half_cosine;
        const double distribution = alpha2 / (pi * denominator * denominator);
        const double visibility =
            0.5 / (cosine + std::sqrt(cosine * cosine * (1 - alpha2) + alpha2));
        const double fresnel_factor = std::pow(1 - half_cosine, 5);
        double attenuation = 1;
        if (item.kind > 0)
        {
            const double fade = std::max(0.0, 1.0 - item.distance / item.range);
            attenuation = fade * fade / (item.distance * item.distance);
            if (item.kind > 1)
            {
                const double t = std::clamp((item.spot_cos - 0.6) / 0.3, 0.0, 1.0);
                attenuation *= t * t * (3 - 2 * t);
            }
        }
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            const double f0 = 0.04 * (1 - item.metallic) + base[channel] * item.metallic;
            const double fresnel = f0 + (1 - f0) * fresnel_factor;
            const double dielectric_fresnel = 0.04 + 0.96 * fresnel_factor;
            const double diffuse =
                (1 - dielectric_fresnel) * (1 - item.metallic) * base[channel] / pi;
            const double reference = item.intensity * attenuation * cosine *
                                     (diffuse + distribution * visibility * fresnel);
            const double actual = basist::half_to_float(pixels[channel]);
            std::fprintf(stderr,
                         "PBR RGBA16F kind %.0f roughness %.3f channel %zu: %.5f "
                         "(reference %.5f)\n",
                         item.kind, item.roughness, channel, actual, reference);
            if (!expect(std::isfinite(actual) &&
                            std::abs(actual - reference) < std::max(0.002, reference * 0.012),
                        "linear HDR direct BRDF and engine light attenuation match reference"))
            {
                return false;
            }
        }
    }
    return true;
}

} // namespace

int main(int, char**)
{
    engine::EngineConfig config;
    config.window_title = "PBR HDR GPU test";
    config.width = 64;
    config.height = 64;
    engine::WorldSystem world;
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows, &world);
    if (!world.initialize(config) || !windows.initialize(config) || !renderer.initialize(config))
    {
        return 1;
    }
    const auto* caps = bgfx::getCaps();
    constexpr auto required_format =
        BGFX_CAPS_FORMAT_TEXTURE_2D | BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER;
    const bool supported =
        caps &&
        (caps->formats[bgfx::TextureFormat::RGBA16F] & required_format) == required_format &&
        caps->limits.maxTextureSize >= 4 && caps->limits.maxFBAttachments >= 1 &&
        bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT) &&
        bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA16F,
                             BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    if (!supported)
    {
        std::fprintf(stderr, "PBR HDR test skipped: RGBA16F rendering, blit or readback "
                             "is unavailable on this device.\n");
    }
    const bool passed = supported && test_pbr_hdr_direct_light();
    renderer.shutdown();
    windows.shutdown();
    world.shutdown();
    return supported ? (passed ? 0 : 1) : 77;
}
