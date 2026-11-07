#include "resource_fixture.hpp"
#include "resource_services.hpp"
#include "scene_snapshot.hpp"

#include "runtime/engine_config.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/post_process_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace
{
using Color = std::array<double, 3>;

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "HDR transparency: %s (SDL: %s)\n", message, SDL_GetError());
    }
    return value;
}

engine::schema::MeshDataT quad()
{
    engine::schema::MeshDataT mesh;
    mesh.path = "meshes/hdr-transparent-quad";
    mesh.vertices = {
        {-1, -1, 0, 0, 0, 1}, {1, -1, 0, 0, 0, 1}, {1, 1, 0, 0, 0, 1}, {-1, 1, 0, 0, 0, 1}};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    mesh.texcoords = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    mesh.tangents.assign(4, {1, 0, 0, 1});
    return mesh;
}

bool frame(engine::WindowSystem& window, engine::RenderSystem& renderer)
{
    return window.tick(1.0 / 60.0) && window.can_render() && renderer.render();
}

bool capture(engine::WindowSystem& window, engine::RenderSystem& renderer,
             engine::SceneCaptureImage& image)
{
    if (!expect(renderer.request_hdr_capture(), "request capture"))
    {
        return false;
    }
    for (int i = 0; i < 32 && renderer.hdr_capture_status() == engine::SceneCaptureStatus::pending;
         ++i)
    {
        if (!expect(frame(window, renderer), "render capture frame"))
        {
            return false;
        }
    }
    return expect(renderer.hdr_capture_status() == engine::SceneCaptureStatus::complete,
                  "capture completed before timeout") &&
           expect(renderer.take_hdr_capture(image), "retrieve completed capture") &&
           expect(image.width >= 32 && image.height >= 32 &&
                      image.rgba.size() == static_cast<std::size_t>(image.width) * image.height * 4,
                  "valid full-resolution floating-point image");
}

// At normal incidence with a white directional radiance of 8, roughness 1,
// metallic 0, F0 = .04, the glTF metallic-roughness BRDF reduces to this.
// This analytic reference is independent of the captured opaque surface.
Color direct_reference(const Color& base)
{
    constexpr double pi = 3.141592653589793;
    Color result{};
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        result[i] = 8.0 * (0.96 * base[i] / pi + 0.04 / (4.0 * pi));
    }
    return result;
}

Color over(const Color& foreground, const Color& background, double alpha)
{
    Color result{};
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        result[i] = foreground[i] * alpha + background[i] * (1.0 - alpha);
    }
    return result;
}

bool check_pixels(const engine::SceneCaptureImage& image, const Color& reference, const char* label,
                  double reference_weight = 1.0)
{
    double max_error = 0.0;
    double sum_error = 0.0;
    double max_alpha_error = 0.0;
    std::size_t count = 0;
    // Exclude the silhouette: coverage and MSAA at geometry edges are a separate concern.
    for (std::uint32_t y = 8; y < image.height - 8; ++y)
    {
        for (std::uint32_t x = 8; x < image.width - 8; ++x)
        {
            const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const auto value = image.rgba[offset + channel];
                if (!std::isfinite(value))
                {
                    return expect(false, "non-finite captured pixel");
                }
                const double error = std::abs(value - reference[channel]);
                max_error = std::max(max_error, error);
                sum_error += error;
                ++count;
            }
            // This is the HDR target's tone-map weight, not coverage alpha.
            // Lit coverage contributes 1 and unlit coverage contributes 0.
            const double alpha = image.rgba[offset + 3];
            if (!std::isfinite(alpha))
            {
                return expect(false, "non-finite tone-map weight");
            }
            max_alpha_error = std::max(max_alpha_error, std::abs(alpha - reference_weight));
        }
    }
    const auto center =
        (static_cast<std::size_t>(image.height / 2) * image.width + image.width / 2) * 4;
    std::fprintf(stderr,
                 "HDR transparency %s (%ux%u): reference %.5f / %.5f / %.5f, center %.5f / %.5f / "
                 "%.5f, RGB samples %zu, MAE %.7f, max %.7f, expected weight %.3f, "
                 "weight max error %.7f\n",
                 label, image.width, image.height, reference[0], reference[1], reference[2],
                 image.rgba[center], image.rgba[center + 1], image.rgba[center + 2], count,
                 sum_error / count, max_error, reference_weight, max_alpha_error);
    // Half-float rendering and blend rounding are the only expected deviations.
    return expect(max_error < 0.025 && max_alpha_error < 0.005, label);
}

bool test()
{
    engine::EngineConfig config;
    config.window_title = "Scene HDR transparency";
    config.width = config.height = 128;
    test_support::ResourceServices services;
    engine::ResourceSystem& resources = services.core;
    engine::WorldSystem world{&services.loader};
    engine::WindowSystem window;
    engine::RenderSystem renderer(window, &world);
    test_support::ResourceFixture fixture;
    if (!(services.initialize(config) && services.prepare_builtins()) ||
        !world.initialize(config) || !window.initialize(config) || !renderer.initialize(config) ||
        !(fixture.publish(resources) && services.prepare_builtins()))
    {
        return expect(false, "initialize renderer");
    }
    auto* scene = world.create_scene("HDR transparency");
    auto camera_node = scene ? scene->create_node("Camera") : engine::Node{};
    auto camera = camera_node ? camera_node.add_component<engine::Camera>() : engine::Camera{};
    auto post = camera_node ? camera_node.add_component<engine::PostProcessComponent>()
                            : engine::PostProcessComponent{};
    auto light_node = scene ? scene->create_node("Directional light") : engine::Node{};
    auto light = light_node ? light_node.add_component<engine::Light>() : engine::Light{};
    engine::CameraSettings lens;
    lens.projection = engine::schema::Projection::orthographic;
    lens.orthographic_size = 1;
    engine::TransformSettings camera_pose;
    camera_pose.position = {0, 0, 3};
    auto lighting = test_support::read_settings(light);
    lighting.intensity = 8;
    auto post_settings = test_support::read_settings(post);
    post_settings.hdr_enabled = true;
    post_settings.bloom_enabled = true;
    post_settings.bloom_threshold = 0.2F;
    post_settings.exposure = 4;
    post_settings.filmic_tonemap = true;
    if (!camera || !post || !light || !camera.set_settings(lens) ||
        !camera_node.get_component<engine::Transform>().set_settings(camera_pose) ||
        !scene->set_active_camera(camera) || !light.set_settings(lighting) ||
        !post.set_settings(post_settings))
    {
        return expect(false, "configure camera, light and post processing");
    }

    constexpr Color background_color{0.3, 0.3, 0.3};
    constexpr Color front_color{0.9, 0.1, 0.2};
    constexpr Color back_color{0.1, 0.2, 0.9};
    constexpr double front_alpha = 0.5;
    constexpr double back_alpha = 0.5;
    std::array<engine::Node, 3> nodes;
    std::array<engine::Material, 3> materials;
    // Create the nearer transparent surface first to detect missing depth sorting.
    const std::array colors{front_color, back_color, background_color};
    const std::array<float, 3> depths{0.3F, 0.0F, -0.3F};
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        nodes[i] = scene->create_node("Surface");
        auto mesh = nodes[i].add_component<engine::MeshRenderer>();
        engine::TransformSettings pose;
        pose.position = {0, 0, depths[i]};
        if (!mesh || !mesh.set_mesh_data(quad()) ||
            !nodes[i].get_component<engine::Transform>().set_settings(pose))
        {
            return expect(false, "create surfaces");
        }
        materials[i] = mesh.material();
        if (!materials[i].set_type("builtin://materials/default-pbr") ||
            !materials[i].set_v4("baseColor", {colors[i][0], colors[i][1], colors[i][2], 1}) ||
            !materials[i].set_float("metallic", 0) || !materials[i].set_float("roughness", 1) ||
            !materials[i].set_float("normalScale", 0))
        {
            return expect(false, "configure PBR materials");
        }
    }
    if (!nodes[0].set_enabled(false) || !nodes[1].set_enabled(false))
    {
        return false;
    }
    for (int i = 0; i < 4; ++i)
    {
        if (!frame(window, renderer))
        {
            return false;
        }
    }
    engine::SceneCaptureImage image;
    const auto background = direct_reference(background_color);
    const auto front = direct_reference(front_color);
    const auto back = direct_reference(back_color);
    const auto expected = over(front, over(back, background, back_alpha), front_alpha);
    if (!expect(expected[0] > 1.0 && expected[2] > 1.0, "test includes HDR values above 1") ||
        !capture(window, renderer, image) || !check_pixels(image, background, "opaque background"))
    {
        return false;
    }
    // Check both sources against absolute references as well as their final composite.
    if (!nodes[2].set_enabled(false) || !nodes[0].set_enabled(true) ||
        !capture(window, renderer, image) || !check_pixels(image, front, "opaque front") ||
        !nodes[0].set_enabled(false) || !nodes[1].set_enabled(true) ||
        !capture(window, renderer, image) || !check_pixels(image, back, "opaque back") ||
        !nodes[2].set_enabled(true) || !nodes[0].set_enabled(true) ||
        !materials[0].set_v4("baseColor",
                             {front_color[0], front_color[1], front_color[2], front_alpha}) ||
        !materials[1].set_v4("baseColor",
                             {back_color[0], back_color[1], back_color[2], back_alpha}))
    {
        return false;
    }
    const auto reversed = over(back, over(front, background, front_alpha), back_alpha);
    if (!expect(std::abs(expected[0] - reversed[0]) > 0.1, "reference distinguishes sort order"))
    {
        return false;
    }
    for (const auto mode :
         {engine::schema::BlendMode::alpha_blend, engine::schema::BlendMode::premultiplied})
    {
        if (!materials[0].set_blend_mode(mode) || !materials[1].set_blend_mode(mode) ||
            !capture(window, renderer, image) ||
            !check_pixels(image, expected,
                          mode == engine::schema::BlendMode::alpha_blend
                              ? "straight alpha sorted composite"
                              : "premultiplied sorted composite"))
        {
            return false;
        }
    }
    // The target alpha is a tone-map weight. An unlit layer contributes zero,
    // so 50% unlit over a lit background leaves a weight of 0.5, while its RGB
    // is still blended as ordinary linear color.
    if (!nodes[1].set_enabled(false) ||
        !materials[0].set_type("builtin://materials/default-unlit") ||
        !materials[0].set_v4("baseColor",
                             {front_color[0], front_color[1], front_color[2], front_alpha}))
    {
        return false;
    }
    const auto unlit_over_lit = over(front_color, background, front_alpha);
    for (const auto mode :
         {engine::schema::BlendMode::alpha_blend, engine::schema::BlendMode::premultiplied})
    {
        if (!materials[0].set_blend_mode(mode) || !capture(window, renderer, image) ||
            !check_pixels(image, unlit_over_lit,
                          mode == engine::schema::BlendMode::alpha_blend
                              ? "straight unlit over lit"
                              : "premultiplied unlit over lit",
                          1.0 - front_alpha))
        {
            return false;
        }
    }
    // Both transparent planes behind opaque depth must leave the background unchanged.
    if (!nodes[1].set_enabled(true))
    {
        return false;
    }
    for (std::size_t i = 0; i < 2; ++i)
    {
        engine::TransformSettings pose;
        pose.position = {0, 0, -0.6F - static_cast<float>(i) * 0.1F};
        if (!nodes[i].get_component<engine::Transform>().set_settings(pose))
        {
            return false;
        }
    }
    return capture(window, renderer, image) && check_pixels(image, background, "opaque occlusion");
}
} // namespace

int main(int, char**)
{
    return test() ? 0 : 1;
}
