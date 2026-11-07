#include "runtime/engine_config.hpp"
#include "runtime/render/render_context.hpp"
#include "runtime/render/render_extension.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/post_process_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"
#include "scene_snapshot.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "hdr_extension_test_frag.bin.h"
#include "hdr_extension_test_vert.bin.h"

namespace
{
bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "HDR extension capture: %s (SDL: %s)\n", message, SDL_GetError());
    }
    return condition;
}

class ColorExtension final : public engine::RenderExtension
{
public:
    ~ColorExtension() noexcept override
    {
        release_gpu();
    }

    [[nodiscard]] bool draw(const engine::RenderContext& context) override
    {
        if (!context.hdr || context.width == 0 || context.height == 0)
        {
            return false;
        }
        if (!enabled)
        {
            return true;
        }
        if (!bgfx::isValid(program_))
        {
            const auto vertex = bgfx::createShader(
                bgfx::copy(hdr_extension_test_vert, sizeof(hdr_extension_test_vert)));
            const auto fragment = bgfx::createShader(
                bgfx::copy(hdr_extension_test_frag, sizeof(hdr_extension_test_frag)));
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
                return false;
            }
            program_ = bgfx::createProgram(vertex, fragment, true);
            constexpr std::array<float, 9> positions{-1, -1, 0, 3, -1, 0, -1, 3, 0};
            bgfx::VertexLayout layout;
            layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
            vertices_ =
                bgfx::createVertexBuffer(bgfx::copy(positions.data(), sizeof(positions)), layout);
        }
        if (!bgfx::isValid(program_) || !bgfx::isValid(vertices_))
        {
            return false;
        }
        bgfx::setScissor(context.width / 4, context.height / 4, context.width / 2,
                         context.height / 2);
        bgfx::setVertexBuffer(0, vertices_);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(context.view_id, program_);
        return true;
    }

    void release_scene() noexcept override
    {
    }

    void release_gpu() noexcept override
    {
        if (bgfx::isValid(vertices_))
        {
            bgfx::destroy(vertices_);
            vertices_ = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(program_))
        {
            bgfx::destroy(program_);
            program_ = BGFX_INVALID_HANDLE;
        }
    }

    bool enabled = false;

private:
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle vertices_ = BGFX_INVALID_HANDLE;
};

bool capture(engine::WindowSystem& window, engine::RenderSystem& renderer,
             engine::SceneCaptureImage& image)
{
    if (!renderer.request_hdr_capture())
    {
        return false;
    }
    for (int frame = 0;
         frame < 32 && renderer.hdr_capture_status() == engine::SceneCaptureStatus::pending;
         ++frame)
    {
        if (!window.tick(1.0 / 60.0) || !window.can_render() || !renderer.render({}))
        {
            return false;
        }
    }
    return renderer.hdr_capture_status() == engine::SceneCaptureStatus::complete &&
           renderer.take_hdr_capture(image);
}

bool verify(const engine::SceneCaptureImage& image, bool extension_enabled)
{
    if (!expect(image.width > 0 && image.height > 0 && image.width % 4 == 0 &&
                    image.height % 4 == 0 &&
                    image.rgba.size() == static_cast<std::size_t>(image.width) * image.height * 4,
                "capture has valid dimensions and float storage"))
    {
        return false;
    }
    constexpr std::array<float, 3> extension{2.0F, 0.75F, 0.125F};
    constexpr std::array<std::uint8_t, 3> clear{32, 64, 96};
    std::array<float, 3> background{};
    for (std::size_t channel = 0; channel < 3; ++channel)
    {
        const float srgb = static_cast<float>(clear[channel]) / 255.0F;
        background[channel] =
            srgb <= 0.04045F ? srgb / 12.92F : std::pow((srgb + 0.055F) / 1.055F, 2.4F);
    }
    float maximum_error = 0;
    for (std::uint32_t y = 0; y < image.height; ++y)
    {
        for (std::uint32_t x = 0; x < image.width; ++x)
        {
            const bool inside = x >= image.width / 4 && x < image.width * 3 / 4 &&
                                y >= image.height / 4 && y < image.height * 3 / 4;
            const auto& expected = extension_enabled && inside ? extension : background;
            const auto index = (static_cast<std::size_t>(y) * image.width + x) * 4;
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const float value = image.rgba[index + channel];
                const float error = std::abs(value - expected[channel]);
                maximum_error = std::max(maximum_error, error);
                if (!std::isfinite(value) || error > 0.002F)
                {
                    std::fprintf(stderr,
                                 "pixel (%u,%u) channel %zu: %.6f expected %.6f, enabled=%d\n", x,
                                 y, channel, value, expected[channel], extension_enabled);
                    return false;
                }
            }
        }
    }
    std::fprintf(stderr, "HDR extension %s: %u x %u pixels, max RGB error %.6f\n",
                 extension_enabled ? "enabled" : "disabled", image.width, image.height,
                 maximum_error);
    return true;
}

bool test()
{
    engine::EngineConfig config;
    config.window_title = "HDR render extension capture";
    config.width = config.height = 128;
    engine::WorldSystem world;
    engine::WindowSystem window;
    ColorExtension extension;
    engine::RenderSystem renderer(window, &world);
    if (!world.initialize(config) || !window.initialize(config) ||
        !renderer.add_extension(extension) || !renderer.initialize(config))
    {
        return expect(false, "initialize renderer and extension");
    }
    auto* scene = world.create_scene("Extension HDR capture");
    auto camera_node = scene ? scene->create_node("Camera") : engine::Node{};
    auto camera = camera_node ? camera_node.add_component<engine::Camera>() : engine::Camera{};
    auto post = camera_node ? camera_node.add_component<engine::PostProcessComponent>()
                            : engine::PostProcessComponent{};
    auto env_node = scene ? scene->create_node("Environment") : engine::Node{};
    auto env = env_node ? env_node.add_component<engine::EnvironmentComponent>()
                        : engine::EnvironmentComponent{};
    if (!scene || !camera || !post || !env || !scene->set_active_camera(camera))
    {
        return expect(false, "create camera and environment");
    }
    auto environment = test_support::read_settings(env);
    environment.clear_color = 0x204060ff;
    if (!env.set_settings(environment))
    {
        return false;
    }
    engine::SceneCaptureImage baseline;
    if (!expect(capture(window, renderer, baseline) && verify(baseline, false),
                "uncovered scene pixels have the expected linear clear color"))
    {
        return false;
    }
    extension.enabled = true;
    engine::SceneCaptureImage extended;
    if (!expect(capture(window, renderer, extended) && verify(extended, true),
                "extension color is captured in the intended region"))
    {
        return false;
    }
    auto settings = test_support::read_settings(post);
    settings.hdr_enabled = true;
    settings.exposure = 4.0F;
    settings.filmic_tonemap = true;
    settings.bloom_enabled = true;
    if (!post.set_settings(settings))
    {
        return false;
    }
    engine::SceneCaptureImage post_processed;
    return expect(capture(window, renderer, post_processed) && verify(post_processed, true),
                  "extension HDR capture precedes display post-processing");
}
} // namespace

int main(int, char**)
{
    return test() ? 0 : 1;
}
