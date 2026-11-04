#include "resource_fixture.hpp"

#include "runtime/engine_config.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/planar_reflection_component.hpp"
#include "runtime/scene/post_process_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL_main.h>
#include <algorithm>
#include <array>
#include <bgfx/bgfx.h>
#include <bimg/bimg.h>
#include <bx/error.h>
#include <bx/math.h>
#include <bx/readerwriter.h>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
engine::schema::TextureDataT constant_normal(const char* path,
                                             const std::array<std::uint16_t, 3>& encoded)
{
    engine::schema::TextureDataT result;
    result.path = path;
    result.width = result.height = 4;
    result.srgb = false;
    result.encoding = engine::schema::TextureEncoding::ktx;
    result.format = engine::schema::TextureFormat::rgba16f;
    std::array<std::uint16_t, 4 * 4 * 4> pixels{};
    for (std::size_t index = 0; index < pixels.size(); index += 4)
    {
        pixels[index] = encoded[0];
        pixels[index + 1] = encoded[1];
        pixels[index + 2] = encoded[2];
        pixels[index + 3] = 0x3c00;
    }
    std::string bytes(68 + sizeof(pixels), '\0');
    bx::StaticMemoryBlockWriter writer(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    bx::Error error;
    const auto written = bimg::imageWriteKtx(&writer, bimg::TextureFormat::RGBA16F, false, 4, 4, 0,
                                             1, 1, false, pixels.data(), &error);
    if (error.isOk() && written == static_cast<std::int32_t>(bytes.size()))
    {
        result.data.assign(bytes.begin(), bytes.end());
    }
    return result;
}

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "atmosphere/hex: %s\n", message);
    }
    return value;
}

bool frames(engine::WindowSystem& window, engine::RenderSystem& renderer, int count)
{
    for (int index = 0; index < count; ++index)
    {
        if (!window.tick(1.0 / 60.0) || !window.can_render() || !renderer.render())
        {
            return false;
        }
    }
    return true;
}

bool capture(engine::WindowSystem& window, engine::RenderSystem& renderer,
             engine::SceneCaptureImage& image)
{
    if (!frames(window, renderer, 3) || !renderer.request_hdr_capture())
    {
        return false;
    }
    for (int index = 0;
         index < 16 && renderer.hdr_capture_status() == engine::SceneCaptureStatus::pending;
         ++index)
    {
        if (!frames(window, renderer, 1))
        {
            return false;
        }
    }
    return renderer.take_hdr_capture(image);
}

bool diffuse_capture(engine::WindowSystem& window, engine::RenderSystem& renderer,
                     const engine::SceneCaptureRequest& request, engine::SceneCaptureImage& image)
{
    if (!expect(renderer.request_diffuse_capture(request), "queue atmosphere bake capture"))
    {
        return false;
    }
    for (int index = 0;
         index < 16 && renderer.diffuse_capture_status() == engine::SceneCaptureStatus::pending;
         ++index)
    {
        if (!frames(window, renderer, 1))
        {
            return false;
        }
    }
    return expect(renderer.take_diffuse_capture(image), "complete atmosphere bake capture");
}

double difference(const engine::SceneCaptureImage& a, const engine::SceneCaptureImage& b,
                  double scale = 1)
{
    if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size())
    {
        return 1e30;
    }
    double total = 0;
    for (std::size_t index = 0; index < a.rgba.size(); ++index)
    {
        if (index % 4 == 3)
        {
            continue;
        }
        if (!std::isfinite(a.rgba[index]) || !std::isfinite(b.rgba[index]))
        {
            return 1e30;
        }
        total += std::abs(a.rgba[index] * scale - b.rgba[index]);
    }
    return total / (a.width * a.height * 3);
}

bool test()
{
    engine::EngineConfig config;
    config.width = 192;
    config.height = 108;
    config.window_title = "Upstream atmosphere and Hex PBR";
    engine::ResourceSystem resources;
    engine::WorldSystem world;
    engine::WindowSystem window;
    engine::RenderSystem renderer(window, &world);
    test_support::ResourceFixture fixture;
    // Exact half-float normals avoid conflating RGBA8's 128/255 bias with normal rotation.
    const auto normal_texture =
        constant_normal("textures/hex-test-flat-normal", {0x3800, 0x3800, 0x3c00});
    const auto tangent_normal =
        constant_normal("textures/hex-test-tangent-normal", {0x3a00, 0x3800, 0x3c00});
    const auto rotated_normal =
        constant_normal("textures/hex-test-rotated-normal", {0x3800, 0x3a00, 0x3c00});
    engine::schema::TextureDataT checker;
    checker.path = "textures/hex-test-checker";
    checker.width = checker.height = 8;
    checker.srgb = true;
    checker.encoding = engine::schema::TextureEncoding::rgba8;
    checker.format = engine::schema::TextureFormat::rgba8;
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            const std::uint8_t value = ((x / 2 + y / 2) % 2) ? 240 : 32;
            checker.data.insert(checker.data.end(), {value, value, value, 255});
        }
    }
    if (!resources.initialize(config) || !world.initialize(config) || !window.initialize(config) ||
        !renderer.initialize(config) || !fixture.add(checker) || !fixture.add(normal_texture) ||
        !fixture.add(tangent_normal) || !fixture.add(rotated_normal) || !fixture.publish(resources))
    {
        return false;
    }
    auto* scene = world.create_scene("Sky only first");
    if (!scene)
    {
        return false;
    }
    auto camera_node = scene->create_node("Camera");
    auto camera = camera_node.add_component<engine::Camera>();
    auto post = camera_node.add_component<engine::PostProcessComponent>();
    auto sun_node = scene->create_node("Sun");
    auto sun = sun_node.add_component<engine::Light>();
    auto environment =
        scene->create_node("Environment").add_component<engine::EnvironmentComponent>();
    engine::TransformSettings camera_transform;
    camera_transform.position = {0, 2, 4};
    engine::TransformSettings sun_transform;
    sun_transform.rotation = {-60, 20, 0};
    auto sky = environment.settings();
    sky.atmosphere_enabled = true;
    sky.atmosphere_sun_node_id = sun_node.info().properties->id;
    auto sun_settings = sun.settings();
    sun_settings.intensity = 1;
    if (!camera || !post || !sun || !environment || !scene->set_active_camera(camera) ||
        !camera_node.get_component<engine::Transform>().set_settings(camera_transform) ||
        !sun_node.get_component<engine::Transform>().set_settings(sun_transform) ||
        !sun.set_settings(sun_settings) || !environment.set_settings(sky))
    {
        return false;
    }
    engine::SceneCaptureImage initial, doubled, exposed;
    if (!capture(window, renderer, initial))
    {
        return false;
    }
    double sky_mean = 0;
    for (std::size_t index = 0; index < initial.rgba.size(); ++index)
    {
        if (index % 4 != 3)
        {
            sky_mean += initial.rgba[index];
        }
    }
    sky_mean /= initial.width * initial.height * 3;
    if (!expect(std::isfinite(sky_mean) && sky_mean > 0.0001,
                "camera+sun+environment renders sky without any placeholder mesh"))
    {
        return false;
    }
    sun_settings.intensity = 2;
    if (!sun.set_settings(sun_settings) || !capture(window, renderer, doubled))
    {
        return false;
    }
    const double scaling_error = difference(initial, doubled, 2);
    std::fprintf(stderr, "Atmosphere mean %.8f; doubled-radiance mean error %.8f\n", sky_mean,
                 scaling_error);
    if (!expect(scaling_error < std::max(0.0005, sky_mean * 0.01),
                "scene HDR sky scales linearly with shared sun radiance"))
    {
        return false;
    }
    auto post_settings = post.settings();
    post_settings.exposure = 3;
    post_settings.filmic_tonemap = true;
    if (!post.set_settings(post_settings) || !capture(window, renderer, exposed) ||
        !expect(difference(doubled, exposed) < 0.0001,
                "sky HDR capture precedes the single display exposure and tone mapping"))
    {
        return false;
    }

    // The offline bake capture uses another eye height and must follow the same solar input.
    engine::SceneCaptureRequest bake;
    bake.width = bake.height = 32;
    bake.view = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -40, -4, 1};
    bx::mtxProj(bake.projection.data(), 60, 1, 0.1F, 1000, bgfx::getCaps()->homogeneousDepth,
                bx::Handedness::Right);
    engine::SceneCaptureImage baked, baked_doubled, main_after_bake;
    if (!diffuse_capture(window, renderer, bake, baked))
    {
        return false;
    }
    sun_settings.intensity = 4;
    if (!sun.set_settings(sun_settings) ||
        !diffuse_capture(window, renderer, bake, baked_doubled) ||
        !expect(difference(baked, baked_doubled, 2) < 0.001,
                "bake sky capture uses its own altitude LUT and shared linear solar radiance"))
    {
        return false;
    }
    sun_settings.intensity = 2;
    if (!sun.set_settings(sun_settings) || !capture(window, renderer, main_after_bake) ||
        !expect(difference(exposed, main_after_bake) < 0.0001,
                "bake sky-view updates do not overwrite the main camera sky"))
    {
        return false;
    }
    if (!renderer.request_diffuse_capture(bake))
    {
        return false;
    }
    auto moved_sun = sun_transform;
    moved_sun.rotation = {-20, 70, 0};
    if (!sun_node.get_component<engine::Transform>().set_settings(moved_sun) ||
        !frames(window, renderer, 2) ||
        !expect(renderer.diffuse_capture_status() == engine::SceneCaptureStatus::cancelled,
                "solar transform changes cancel an in-flight bake even without requested scene "
                "lights") ||
        !sun_node.get_component<engine::Transform>().set_settings(sun_transform))
    {
        return false;
    }

    // The saved atmosphere request remains enabled, but an unusable sun must not publish a
    // background-less bake. Normal rendering must keep running so the caller can recover.
    engine::SceneCaptureImage rejected_bake;
    if (!sun.set_enabled(false) || !frames(window, renderer, 2) ||
        !expect(renderer.settings().atmosphere.atmosphere_enabled &&
                    !renderer.settings().atmosphere_sun_valid &&
                    !renderer.settings().atmosphere_submitted &&
                    !renderer.request_diffuse_capture(bake) &&
                    !renderer.take_diffuse_capture(rejected_bake) && rejected_bake.rgba.empty(),
                "disabled atmosphere sun rejects an explicit bake while the main view remains "
                "usable") ||
        !sun.set_enabled(true) || !frames(window, renderer, 2))
    {
        return false;
    }

    auto water_node = scene->create_node("Sky reflection plane");
    auto water_mesh = water_node.add_component<engine::MeshRenderer>();
    auto reflection = water_node.add_component<engine::PlanarReflectionComponent>();
    engine::schema::MeshDataT floor;
    floor.path = "meshes/sky-test-floor";
    floor.vertices = {
        {-8, 0, -8, 0, 1, 0}, {8, 0, -8, 0, 1, 0}, {8, 0, 8, 0, 1, 0}, {-8, 0, 8, 0, 1, 0}};
    floor.indices = {0, 2, 1, 0, 3, 2};
    floor.texcoords = {{0, 0}, {2, 0}, {2, 2}, {0, 2}};
    floor.tangents.assign(4, {1, 0, 0, -1});
    if (!water_mesh || !reflection || !water_mesh.set_mesh_data(floor) ||
        !water_mesh.material().set_type("builtin://materials/default-water") ||
        !water_mesh.material().set_texture("normalTexture", normal_texture.path) ||
        !reflection.set_enabled(false))
    {
        return false;
    }
    engine::SceneCaptureImage no_mirror, mirror, no_mirror_doubled, mirror_doubled;
    if (!capture(window, renderer, no_mirror) || !reflection.set_enabled(true) ||
        !capture(window, renderer, mirror))
    {
        return false;
    }
    const double reflected_sky = difference(no_mirror, mirror);
    sun_settings.intensity = 4;
    if (!sun.set_settings(sun_settings) || !reflection.set_enabled(false) ||
        !capture(window, renderer, no_mirror_doubled) || !reflection.set_enabled(true) ||
        !capture(window, renderer, mirror_doubled))
    {
        return false;
    }
    // Isolate reflection from the water's existing absorption/body color and refracted scene.
    // Those independent terms must not be mistaken for solar reflection energy.
    double mirror_scale_error = 0;
    for (std::size_t index = 0; index < mirror.rgba.size(); ++index)
    {
        if (index % 4 == 3)
        {
            continue;
        }
        mirror_scale_error +=
            std::abs(2 * (mirror.rgba[index] - no_mirror.rgba[index]) -
                     (mirror_doubled.rgba[index] - no_mirror_doubled.rgba[index]));
    }
    mirror_scale_error /= mirror.width * mirror.height * 3;
    std::fprintf(stderr, "Sky planar mean contribution %.8f; solar scaling error %.8f\n",
                 reflected_sky, mirror_scale_error);
    if (!expect(reflected_sky > 0.0001 && mirror_scale_error < 0.001,
                "water receives the live atmosphere through an isolated mirrored-view LUT"))
    {
        return false;
    }
    sun_settings.intensity = 2;
    if (!sun.set_settings(sun_settings) || !scene->destroy_node(water_node.id()))
    {
        return false;
    }

    sky.atmosphere_enabled = false;
    sky.ambient_intensity = 1;
    if (!environment.set_settings(sky))
    {
        return false;
    }
    auto surface_node = scene->create_node("Hex PBR surface");
    auto mesh = surface_node.add_component<engine::MeshRenderer>();
    engine::schema::MeshDataT quad;
    quad.path = "meshes/hex-test-surface";
    quad.vertices = {
        {-4, -4, 0, 0, 0, 1}, {4, -4, 0, 0, 0, 1}, {4, 4, 0, 0, 0, 1}, {-4, 4, 0, 0, 0, 1}};
    quad.indices = {0, 1, 2, 0, 2, 3};
    quad.texcoords = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    quad.tangents.assign(4, {1, 0, 0, 1});
    if (!mesh || !mesh.set_mesh_data(quad))
    {
        return false;
    }
    auto material = mesh.material();
    if (!material.set_texture("normalTexture", normal_texture.path))
    {
        return false;
    }
    engine::SceneCaptureImage regular, flat_hex, patterned, enabled, disabled;
    if (!capture(window, renderer, regular) || !material.set_float("hexTilingEnabled", 1) ||
        !capture(window, renderer, flat_hex) ||
        !expect(difference(regular, flat_hex) < 0.0005,
                "randomly rotated flat normal and constant ORM preserve the standard PBR response"))
    {
        return false;
    }
    if (!material.set_float("hexTilingEnabled", 0) ||
        !material.set_texture("baseColorTexture", checker.path) ||
        !capture(window, renderer, patterned) || !material.set_float("hexTilingEnabled", 1) ||
        !capture(window, renderer, enabled) || !material.set_float("hexTilingEnabled", 0) ||
        !capture(window, renderer, disabled))
    {
        return false;
    }
    const double changed = difference(patterned, enabled);
    const double restored = difference(patterned, disabled);
    std::fprintf(stderr, "Hex patterned mean difference %.8f; disabled restoration %.8f\n", changed,
                 restored);
    if (!expect(
            changed > 0.001 && restored < 0.0005,
            "Hex changes repeated texture sampling and disabling it restores the original pixels"))
    {
        return false;
    }
    // At UV=0 the upstream triangular grid selects vertex (0,0), whose rotation is pi/2
    // for strength 0.5. With bgfx's shader matrix convention the UV sample turns clockwise,
    // so a +T normal becomes +B, verified against an independently authored normal map.
    if (!material.set_texture("baseColorTexture", "builtin://textures/white-srgb") ||
        !material.set_v4("uvTransform", {0, 0, 0, 0}) ||
        !material.set_texture("normalTexture", rotated_normal.path))
    {
        return false;
    }
    engine::SceneCaptureImage expected_rotated, actual_rotated, source_regular, negative_regular,
        negative_hex;
    if (!capture(window, renderer, expected_rotated) ||
        !material.set_texture("normalTexture", tangent_normal.path) ||
        !capture(window, renderer, source_regular) || !material.set_float("hexTilingEnabled", 1) ||
        !capture(window, renderer, actual_rotated))
    {
        return false;
    }
    const double rotation_error = difference(expected_rotated, actual_rotated);
    std::fprintf(stderr, "Hex analytic quarter-turn normal mean error %.8f\n", rotation_error);
    if (!expect(rotation_error < 0.0005 && difference(source_regular, actual_rotated) > 0.001,
                "Hex rotates directional normal derivatives back into the material tangent frame"))
    {
        return false;
    }
    if (!material.set_float("hexTilingEnabled", 0) ||
        !material.set_v4("uvTransform", {-2, -3, 0.3, 0.7}) ||
        !material.set_float("hexTilingRotation", 0) ||
        !capture(window, renderer, negative_regular) ||
        !material.set_float("hexTilingEnabled", 1) || !capture(window, renderer, negative_hex) ||
        !expect(difference(negative_regular, negative_hex) < 0.0005,
                "negative UV scale keeps directional normal handedness when random rotation is "
                "disabled"))
    {
        return false;
    }
    if (!world.destroy_scene(scene->id()) || !frames(window, renderer, 3) ||
        !expect(!renderer.settings().atmosphere.atmosphere_enabled,
                "scene retirement discards the atmosphere request and GPU ownership"))
    {
        return false;
    }
    return true;
}
} // namespace

int main(int, char**)
{
    return test() ? 0 : 1;
}
