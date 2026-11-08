#include "../../support/render_fixture.hpp"
#include "resource_fixture.hpp"
#include "resource_services.hpp"
#include "scene_snapshot.hpp"

#include "runtime/engine_config.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/post_process_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <bimg/bimg.h>
#include <bx/error.h>
#include <bx/readerwriter.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "HDR scene capture: %s (SDL: %s)\n", message, SDL_GetError());
    }
    return condition;
}

bool frames(engine::WindowSystem& window, engine::RenderSystem& renderer, int count,
            engine::Camera camera = {})
{
    for (int i = 0; i < count; ++i)
    {
        if (!window.tick(1.0 / 60.0) || !window.can_render() ||
            !test_support::render_frame(renderer, camera))
        {
            return false;
        }
    }
    return true;
}

engine::schema::TextureDataT environment_texture(const char* path, bool cube)
{
    const std::size_t channels = cube ? 4 : 2;
    const std::size_t count = 4 * 4 * (cube ? 6 : 1);
    std::vector<std::uint8_t> pixels(count * channels * 2);
    for (std::size_t i = 0; i < count; ++i)
    {
        for (std::size_t channel = 0; channel < channels; ++channel)
        {
            const std::uint16_t half = cube ? 0x3c00 : (channel == 0 ? 0x3800 : 0x3400);
            const auto offset = (i * channels + channel) * 2;
            pixels[offset] = static_cast<std::uint8_t>(half & 0xff);
            pixels[offset + 1] = static_cast<std::uint8_t>(half >> 8);
        }
    }
    engine::schema::TextureDataT result;
    result.schema_version = 2;
    result.path = path;
    result.width = result.height = 4;
    result.mip_count = 1;
    result.srgb = false;
    result.cube_map = cube;
    result.encoding = engine::schema::TextureEncoding::native;
    result.format =
        cube ? engine::schema::TextureFormat::rgba16f : engine::schema::TextureFormat::rg16f;
    result.data = std::move(pixels);
    const auto face_bytes = 4 * 4 * channels * 2;
    for (std::uint32_t face = 0; face < (cube ? 6U : 1U); ++face)
    {
        result.subresources.emplace_back(0, face, face * face_bytes, face_bytes);
    }
    return result;
}

engine::schema::TextureDataT albedo(const char* path, const std::array<std::uint8_t, 3>& rgb)
{
    engine::schema::TextureDataT result;
    result.schema_version = 2;
    result.path = path;
    result.width = result.height = 4;
    result.srgb = true;
    result.encoding = engine::schema::TextureEncoding::native;
    result.format = engine::schema::TextureFormat::rgba8;
    result.subresources.emplace_back(0, 0, 0,
                                     static_cast<std::uint64_t>(result.width) * result.height * 4);
    for (int i = 0; i < 16; ++i)
    {
        result.data.insert(result.data.end(), rgb.begin(), rgb.end());
        result.data.push_back(255);
    }
    return result;
}

engine::schema::MeshDataT quad()
{
    engine::schema::MeshDataT result;
    result.path = "meshes/hdr-capture-quad";
    result.vertices = {
        {-1, -1, 0, 0, 0, 1}, {1, -1, 0, 0, 0, 1}, {1, 1, 0, 0, 0, 1}, {-1, 1, 0, 0, 0, 1}};
    result.indices = {0, 1, 2, 0, 2, 3};
    result.texcoords = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    result.tangents.assign(4, {1, 0, 0, 1});
    return result;
}

bool capture(engine::WindowSystem& window, engine::RenderSystem& renderer,
             engine::SceneCaptureImage& image, engine::Camera camera = {})
{
    if (!expect(renderer.request_hdr_capture() && !renderer.request_hdr_capture(),
                "one pending request is accepted") ||
        !expect(!renderer.take_hdr_capture(image) && image.rgba.empty(),
                "pending image is inaccessible"))
    {
        return false;
    }
    for (int i = 0; i < 32 && renderer.hdr_capture_status() == engine::SceneCaptureStatus::pending;
         ++i)
    {
        if (!frames(window, renderer, 1, camera))
        {
            return false;
        }
    }
    return expect(renderer.hdr_capture_status() == engine::SceneCaptureStatus::complete &&
                      !renderer.request_hdr_capture() && renderer.take_hdr_capture(image) &&
                      renderer.hdr_capture_status() == engine::SceneCaptureStatus::idle &&
                      image.width > 0 && image.height > 0 &&
                      image.rgba.size() == static_cast<std::size_t>(image.width) * image.height * 4,
                  "completed float image is transferred once");
}

bool test()
{
    engine::EngineConfig config;
    config.window_title = "Scene HDR capture";
    config.width = config.height = 128;
    test_support::ResourceServices services;
    auto& resources = services.core;
    engine::WorldSystem world(&services.loader);
    engine::WindowSystem window;
    engine::RenderSystem renderer(window, &world);
    test_support::ResourceFixture fixture;
    constexpr std::array<std::uint8_t, 3> top{224, 176, 128};
    constexpr std::array<std::uint8_t, 3> bottom{128, 176, 224};
    const auto irr = environment_texture("textures/capture-irradiance", true);
    const auto pre = environment_texture("textures/capture-prefilter", true);
    const auto lut = environment_texture("textures/capture-lut", false);
    const auto top_texture = albedo("textures/capture-top", top);
    const auto bottom_texture = albedo("textures/capture-bottom", bottom);
    if (!expect(!renderer.request_hdr_capture(), "reject request before initialization") ||
        !(services.initialize(config) && services.prepare_builtins()) ||
        !world.initialize(config) || !window.initialize(config) || !renderer.initialize(config) ||
        !fixture.add(irr) || !fixture.add(pre) || !fixture.add(lut) || !fixture.add(top_texture) ||
        !fixture.add(bottom_texture) ||
        !services.publish_and_load(fixture.catalog, {{"content", fixture.root}}) ||
        !expect(!renderer.request_hdr_capture(), "reject request without scene"))
    {
        return false;
    }
    auto* scene = world.create_scene("HDR capture integration");
    auto camera_node = scene ? scene->create_node("Camera") : engine::Node{};
    auto camera = camera_node ? camera_node.add_component<engine::Camera>() : engine::Camera{};
    auto post = camera_node ? camera_node.add_component<engine::PostProcessComponent>()
                            : engine::PostProcessComponent{};
    auto env_node = scene ? scene->create_node("Environment") : engine::Node{};
    auto env = env_node ? env_node.add_component<engine::EnvironmentComponent>()
                        : engine::EnvironmentComponent{};
    auto light_node = scene ? scene->create_node("Light") : engine::Node{};
    auto light = light_node ? light_node.add_component<engine::Light>() : engine::Light{};
    engine::CameraSettings lens;
    lens.projection = engine::schema::Projection::orthographic;
    lens.orthographic_size = 1;
    engine::TransformSettings camera_pose;
    camera_pose.position = {0, 0, 3};
    auto environment = test_support::read_settings(env);
    environment.ibl_enabled = true;
    environment.ibl_intensity = 1;
    auto lighting = test_support::read_settings(light);
    lighting.intensity = 8;
    if (!camera || !post || !env || !light || !camera.set_settings(lens) ||
        !camera_node.get_component<engine::Transform>().set_settings(camera_pose) ||
        !scene->set_active_camera(camera) || !light.set_settings(lighting) ||
        !resources.catalog().identities().path_to_guid(irr.path, environment.ibl_irradiance_guid) ||
        !resources.catalog().identities().path_to_guid(pre.path, environment.ibl_prefilter_guid) ||
        !resources.catalog().identities().path_to_guid(lut.path, environment.ibl_lut_guid) ||
        !env.set_settings(environment))
    {
        return false;
    }
    for (int i = 0; i < 2; ++i)
    {
        auto node = scene->create_node(i == 0 ? "Top" : "Bottom");
        auto mesh = node.add_component<engine::MeshRenderer>();
        engine::TransformSettings pose;
        pose.position = {0, i == 0 ? 0.5 : -0.5, 0};
        pose.scale = {1, 0.5, 1};
        if (!mesh || !mesh.set_mesh_data(quad()) ||
            !node.get_component<engine::Transform>().set_settings(pose) ||
            !mesh.material().set_type("builtin://materials/default-pbr") ||
            !mesh.material().set_texture("baseColorTexture",
                                         i == 0 ? top_texture.path : bottom_texture.path) ||
            !mesh.material().set_v4("baseColor", {1, 1, 1, 1}) ||
            !mesh.material().set_float("metallic", 0) ||
            !mesh.material().set_float("roughness", 1) ||
            !mesh.material().set_float("normalScale", 0))
        {
            return false;
        }
    }
    if (!frames(window, renderer, 4))
    {
        return false;
    }
    engine::SceneCaptureImage image;
    if (!capture(window, renderer, image))
    {
        return false;
    }
    const auto srgb = [](double value)
    { return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4); };
    const auto reflected = [](double f0)
    {
        const double single = 0.5 * f0 + 0.25;
        const double average = f0 + (1.0 - f0) / 21.0;
        return single + 0.25 * single * average / (1.0 - 0.25 * average);
    };
    constexpr double pi = 3.141592653589793;
    const auto check_image = [&](const engine::SceneCaptureImage& actual)
    {
        for (int side = 0; side < 2; ++side)
        {
            const auto& source = side == 0 ? top : bottom;
            const auto y = side == 0 ? actual.height / 4 : actual.height * 3 / 4;
            const auto index = (static_cast<std::size_t>(y) * actual.width + actual.width / 2) * 4;
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const auto color = srgb(static_cast<double>(source[channel]) / 255.0);
                const auto reference = color * (1 - reflected(0.04)) + reflected(0.04) +
                                       8 * (0.96 * color / pi + 0.04 / (4 * pi));
                const auto value = actual.rgba[index + channel];
                std::fprintf(stderr, "Scene HDR %s channel %zu: %.5f vs %.5f\n",
                             side == 0 ? "top" : "bottom", channel, value, reference);
                if (!expect(std::isfinite(value) && std::abs(value - reference) < 0.025,
                            "linear scene pixels match material, direct light and IBL"))
                {
                    return false;
                }
            }
        }
        return true;
    };
    if (!check_image(image))
    {
        return false;
    }
    auto post_settings = test_support::read_settings(post);
    post_settings.hdr_enabled = true;
    post_settings.bloom_enabled = true;
    post_settings.bloom_threshold = 0.2F;
    post_settings.exposure = 4;
    post_settings.filmic_tonemap = true;
    engine::SceneCaptureImage before_post;
    if (!post.set_settings(post_settings) || !capture(window, renderer, before_post) ||
        !check_image(before_post))
    {
        return false;
    }
    // A caller-supplied camera may belong to an independent world and the
    // gameplay scene need not have an active camera at request time.
    engine::WorldSystem preview_world;
    if (!preview_world.initialize(config))
    {
        return false;
    }
    auto* preview_scene = preview_world.create_scene("External view");
    auto preview_node = preview_scene ? preview_scene->create_node("Camera") : engine::Node{};
    auto preview_camera =
        preview_node ? preview_node.add_component<engine::Camera>() : engine::Camera{};
    engine::SceneCaptureImage external_view;
    if (!preview_camera || !preview_camera.set_settings(lens) ||
        !preview_node.get_component<engine::Transform>().set_settings(camera_pose) ||
        !scene->set_active_camera({}) || !renderer.request_hdr_capture() ||
        !frames(window, renderer, 1) ||
        !expect(renderer.hdr_capture_status() == engine::SceneCaptureStatus::cancelled,
                "actual frame without any camera cancels pending capture") ||
        !capture(window, renderer, external_view, preview_camera) || !check_image(external_view) ||
        !renderer.request_hdr_capture() || !frames(window, renderer, 1, preview_camera))
    {
        return false;
    }
    // Once the valid frame is submitted, later frames without a camera must
    // not discard it while the GPU readback is still in flight.
    if (!frames(window, renderer, 1))
    {
        return false;
    }
    for (int i = 0; i < 32 && renderer.hdr_capture_status() == engine::SceneCaptureStatus::pending;
         ++i)
    {
        if (!frames(window, renderer, 1))
        {
            return false;
        }
    }
    if (!expect(renderer.hdr_capture_status() == engine::SceneCaptureStatus::complete &&
                    renderer.take_hdr_capture(external_view),
                "submitted HDR capture survives later frames without a camera") ||
        !check_image(external_view) || !scene->set_active_camera(camera))
    {
        return false;
    }
    for (const std::uint8_t samples : {2, 4, 8})
    {
        if (renderer.supports_msaa_samples(samples))
        {
            engine::SceneCaptureImage before_rebuild;
            if (!renderer.request_hdr_capture() || !frames(window, renderer, 1) ||
                !renderer.set_msaa_samples(samples) || !frames(window, renderer, 1))
            {
                return false;
            }
            for (int i = 0;
                 i < 32 && renderer.hdr_capture_status() == engine::SceneCaptureStatus::pending;
                 ++i)
            {
                if (!frames(window, renderer, 1))
                {
                    return false;
                }
            }
            if (!expect(renderer.hdr_capture_status() == engine::SceneCaptureStatus::complete &&
                            renderer.take_hdr_capture(before_rebuild),
                        "in-flight capture survives later MSAA target rebuild") ||
                !check_image(before_rebuild))
            {
                return false;
            }
            engine::SceneCaptureImage multisampled;
            if (!capture(window, renderer, multisampled) || !check_image(multisampled))
            {
                return false;
            }
            std::fprintf(stderr, "Scene HDR MSAA %u verified\n", samples);
            break;
        }
    }
    if (!expect(renderer.request_hdr_capture(), "queue cancellation before render"))
    {
        return false;
    }
    renderer.cancel_hdr_capture();
    if (!expect(renderer.hdr_capture_status() == engine::SceneCaptureStatus::cancelled &&
                    !renderer.take_hdr_capture(image) && image.rgba.empty(),
                "cancelled capture cannot publish data") ||
        !expect(renderer.request_hdr_capture(), "new request after cancellation") ||
        !frames(window, renderer, 1))
    {
        return false;
    }
    renderer.cancel_hdr_capture();
    if (!expect(renderer.hdr_capture_status() == engine::SceneCaptureStatus::cancelled,
                "cancel submitted GPU capture") ||
        !frames(window, renderer, 8) ||
        !expect(renderer.hdr_capture_status() == engine::SceneCaptureStatus::cancelled,
                "retired readback does not revive cancelled result") ||
        !expect(renderer.request_hdr_capture(), "request after old readback retires") ||
        !frames(window, renderer, 1))
    {
        return false;
    }
    world.destroy_scene();
    if (!frames(window, renderer, 1) ||
        !expect(renderer.hdr_capture_status() == engine::SceneCaptureStatus::cancelled &&
                    !renderer.take_hdr_capture(image),
                "scene retirement cancels old capture"))
    {
        return false;
    }
    auto* replacement = world.create_scene("Shutdown during readback");
    auto replacement_camera_node =
        replacement ? replacement->create_node("Camera") : engine::Node{};
    auto replacement_camera = replacement_camera_node
                                  ? replacement_camera_node.add_component<engine::Camera>()
                                  : engine::Camera{};
    auto replacement_node = replacement ? replacement->create_node("Surface") : engine::Node{};
    auto replacement_mesh = replacement_node
                                ? replacement_node.add_component<engine::MeshRenderer>()
                                : engine::MeshRenderer{};
    if (!replacement_camera || !replacement_mesh || !replacement_camera.set_settings(lens) ||
        !replacement_camera_node.get_component<engine::Transform>().set_settings(camera_pose) ||
        !replacement->set_active_camera(replacement_camera) ||
        !replacement_mesh.set_mesh_data(quad()) || !frames(window, renderer, 2) ||
        !renderer.request_hdr_capture() || !frames(window, renderer, 1))
    {
        return false;
    }
    renderer.shutdown();
    return expect(renderer.hdr_capture_status() == engine::SceneCaptureStatus::cancelled &&
                      !renderer.take_hdr_capture(image),
                  "shutdown retires the GPU request and does not expose stale pixels");
}
} // namespace

int main(int, char**)
{
    return test() ? 0 : 1;
}
