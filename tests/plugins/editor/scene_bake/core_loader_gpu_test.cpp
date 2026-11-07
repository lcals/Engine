#include "resource_fixture.hpp"
#include "resource_services.hpp"
#include "scene_snapshot.hpp"

#include "plugins/editor/scene_bake/ibl_bake_job.hpp"
#include "plugins/editor/scene_bake/lighting_bake_job.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/codecs/texture_resource_codec.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL_main.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <string>

namespace
{
namespace bake = engine::plugins::scene_bake;

bool expect(bool value, const char* message, const std::string& error = {})
{
    if (!value)
    {
        std::fprintf(stderr, "scene bake core loader: %s: %s\n", message, error.c_str());
    }
    return value;
}

engine::schema::TextureDataT color_texture(std::string path, std::uint32_t width,
                                           std::uint32_t height, std::array<std::uint8_t, 4> color)
{
    engine::schema::TextureDataT result;
    result.path = std::move(path);
    result.width = width;
    result.height = height;
    result.encoding = engine::schema::TextureEncoding::rgba8;
    result.format = engine::schema::TextureFormat::rgba8;
    result.srgb = false;
    for (std::uint32_t index = 0; index < width * height; ++index)
    {
        result.data.insert(result.data.end(), color.begin(), color.end());
    }
    return result;
}

double mean_red(const engine::schema::TextureDataT& texture)
{
    if (!engine::TextureResourceCodec::validate(texture) ||
        texture.format != engine::schema::TextureFormat::rgba16f || texture.data.size() < 68)
    {
        return -1;
    }
    // The codec checks the complete KTX layout. Inspect mip zero of its first face.
    std::uint32_t metadata = 0;
    std::memcpy(&metadata, texture.data.data() + 60, sizeof(metadata));
    const std::size_t begin = 68 + metadata;
    const std::size_t pixels = std::size_t{texture.width} * texture.height;
    if (begin > texture.data.size() || pixels * 8 > texture.data.size() - begin)
    {
        return -1;
    }
    double sum = 0;
    for (std::size_t index = 0; index < pixels; ++index)
    {
        std::uint16_t red = 0;
        std::memcpy(&red, texture.data.data() + begin + index * 8, sizeof(red));
        const float value = bx::halfToFloat(red);
        if (!std::isfinite(value) || value < 0)
        {
            return -1;
        }
        sum += value;
    }
    return sum / pixels;
}

bool frame(engine::WindowSystem& windows, engine::RenderSystem& renderer)
{
    return windows.tick(1.0 / 60.0) && windows.can_render() && renderer.render();
}

bool exercise()
{
    engine::EngineConfig config;
    config.width = config.height = 96;
    config.window_title = "Scene bake with the core single-resource loader";
    engine::ThreadSystem threads;
    engine::TaskSystem tasks(threads);
    test_support::ResourceServices services(&threads);
    auto& resources = services.core;
    engine::WorldSystem world(&services.loader);
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows, &world);
    test_support::ResourceFixture fixture;
    // Jobs must outlive the renderer shutdown so outstanding readback memory stays valid.
    bake::IblBakeJob ibl;
    bake::LightingBakeJob lighting(renderer, resources, tasks);
    const auto cleanup = [&]
    {
        lighting.shutdown();
        ibl.shutdown();
        renderer.shutdown();
        windows.shutdown();
        world.shutdown();
        resources.shutdown();
        tasks.shutdown();
        threads.shutdown();
    };
    // Keep cleanup on every early return without exposing any test-only production API.
    struct Cleanup
    {
        const decltype(cleanup)& run;
        ~Cleanup()
        {
            run();
        }
    } guard{cleanup};
    if (!expect(threads.initialize(config) && tasks.initialize(config) &&
                    (services.initialize(config) && services.prepare_builtins()) &&
                    world.initialize(config) && windows.initialize(config) &&
                    renderer.initialize(config),
                "initialize real renderer and the core loader") ||
        !expect(world.resource_loader() == &services.loader,
                "world borrows the explicitly injected resource loader"))
    {
        return false;
    }
    const auto panorama = color_texture("textures/white-panorama", 16, 8, {255, 255, 255, 255});
    const auto tint = color_texture("textures/red-albedo", 2, 2, {230, 20, 10, 255});
    engine::schema::MeshDataT geometry;
    geometry.path = "meshes/bake-quad";
    geometry.vertices = {
        {-1, -1, 0, 0, 0, 1}, {1, -1, 0, 0, 0, 1}, {1, 1, 0, 0, 0, 1}, {-1, 1, 0, 0, 0, 1}};
    geometry.indices = {0, 1, 2, 0, 2, 3};
    geometry.texcoords = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    geometry.tangents.assign(4, {1, 0, 0, 1});
    engine::schema::MaterialDataT material;
    material.type_path = "materials/textured-bake";
    material.shader_path = "builtin://shaders/pbr";
    auto albedo = std::make_unique<engine::schema::MaterialTextureT>();
    albedo->name = "baseColorTexture";
    albedo->texture_path = tint.path;
    material.textures.push_back(std::move(albedo));
    if (!expect(fixture.add(panorama) && fixture.add(tint) && fixture.add(geometry) &&
                    fixture.add(material) &&
                    services.publish_and_load(fixture.catalog, {{"content", fixture.root}}),
                "publish formal GUID resources and a filesystem mount",
                std::string(resources.last_error())))
    {
        return false;
    }
    engine::ResourceReference source;
    if (!expect(services.load(panorama.path, source) && source &&
                    source.data<engine::TextureResource>() && source.handle().id != 0,
                "load an IBL input from its mounted entity without graph residency"))
    {
        return false;
    }
    std::string error;
    if (!expect(ibl.start(*source.data<engine::TextureResource>(), 16, 16, 32, error),
                "start GPU IBL bake", error))
    {
        return false;
    }
    source.reset();
    auto status = bake::BakeProgress::running;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (status == bake::BakeProgress::running && std::chrono::steady_clock::now() < deadline)
    {
        status = ibl.tick(renderer.frame_number(), error);
        if (!frame(windows, renderer))
        {
            return false;
        }
    }
    engine::schema::TextureDataT irradiance;
    irradiance.path = "baked/irradiance";
    irradiance.width = irradiance.height = 16;
    irradiance.cube_map = true;
    irradiance.srgb = false;
    irradiance.format = engine::schema::TextureFormat::rgba16f;
    irradiance.encoding = engine::schema::TextureEncoding::ktx;
    irradiance.data = ibl.outputs()[0];
    const double ibl_mean = mean_red(irradiance);
    if (!expect(status == bake::BakeProgress::complete && ibl.completed() == ibl.total() &&
                    std::abs(ibl_mean - 1) < 0.01 && !ibl.outputs()[1].empty() &&
                    !ibl.outputs()[2].empty(),
                "real GPU IBL readback preserves a white environment", error))
    {
        return false;
    }
    auto* scene = world.create_scene("Core loader bake fixture");
    if (!scene)
    {
        return false;
    }
    auto camera_node = scene->create_node("Camera");
    auto camera = camera_node.add_component<engine::Camera>();
    auto environment =
        scene->create_node("Environment").add_component<engine::EnvironmentComponent>();
    auto mesh_node = scene->create_node("Static textured quad");
    auto mesh = mesh_node.add_component<engine::MeshRenderer>();
    engine::TransformSettings eye;
    eye.position = {0, 0, 3};
    auto ambient = test_support::read_settings(environment);
    ambient.ambient_color = {1, 1, 1, 1};
    ambient.ambient_intensity = 1;
    if (!expect(camera && environment && mesh && scene->set_active_camera(camera) &&
                    camera_node.get_component<engine::Transform>().set_settings(eye) &&
                    environment.set_settings(ambient) && mesh.set_mesh(geometry.path) &&
                    mesh.material().set_type(material.type_path) && mesh.set_bake_static(true) &&
                    mesh.material().texture("baseColorTexture").id != 0 && frame(windows, renderer),
                "components retain mesh, material, shader and texture using single-item loads"))
    {
        return false;
    }
    engine::SceneCaptureRequest request;
    request.width = request.height = 32;
    bx::mtxLookAt(request.view.data(), {0, 0, 3}, {0, 0, 0}, {0, 1, 0}, bx::Handedness::Right);
    bx::mtxProj(request.projection.data(), 50, 1, 0.1F, 10, bgfx::getCaps()->homogeneousDepth,
                bx::Handedness::Right);
    request.node_ids.push_back(mesh_node.id());
    if (!expect(renderer.request_diffuse_capture(request), "queue textured diffuse capture"))
    {
        return false;
    }
    for (unsigned index = 0;
         index < 24 && renderer.diffuse_capture_status() == engine::SceneCaptureStatus::pending;
         ++index)
    {
        if (!frame(windows, renderer))
        {
            return false;
        }
    }
    engine::SceneCaptureImage captured;
    if (!expect(renderer.take_diffuse_capture(captured), "read back the actual textured scene"))
    {
        return false;
    }
    const auto center =
        (std::size_t{captured.height} / 2 * captured.width + captured.width / 2) * 4;
    if (!expect(captured.rgba[center] > captured.rgba[center + 1] * 3 &&
                    captured.rgba[center] > 0.01F,
                "capture uses the loaded red texture, not a missing/default material"))
    {
        return false;
    }
    engine::schema::SceneDataT saved;
    bake::LightingBakeSettings settings;
    settings.lightmap_resolution = settings.hemisphere_resolution = 16;
    settings.bounce_count = 1;
    settings.interpolation_passes = 4;
    settings.source_fingerprint = "core-loader-fixture";
    if (!expect(scene->build_data(saved) && lighting.start(saved, *scene, settings, error),
                "start lighting bake from saved identities with the core loader", error))
    {
        return false;
    }
    status = bake::BakeProgress::running;
    while (status == bake::BakeProgress::running && std::chrono::steady_clock::now() < deadline)
    {
        status = lighting.tick(error);
        if (!frame(windows, renderer))
        {
            return false;
        }
    }
    if (!expect(status == bake::BakeProgress::complete &&
                    lighting.completed() == lighting.total() &&
                    lighting.outputs().meshes.size() == 1,
                "complete UV2, renderer hemisphere captures and lightmapper integration", error))
    {
        return false;
    }
    const auto& baked = lighting.outputs().meshes.front();
    const double lighting_mean = baked.lightmap ? mean_red(*baked.lightmap) : -1;
    if (!expect(
            baked.mesh && baked.mesh->lightmap_texcoords.size() == baked.mesh->vertices.size() &&
                std::abs(lighting_mean - std::numbers::pi) < 0.03 && fixture.add(*baked.lightmap) &&
                services.publish_and_load(fixture.catalog, {{"content", fixture.root}}) &&
                services.load(baked.lightmap->path, source) && source &&
                source.data<engine::TextureResource>() &&
                mean_red(*source.data<engine::TextureResource>()) == lighting_mean,
            "load the baked lightmap entity again without a graph",
            std::string(resources.last_error())))
    {
        return false;
    }
    std::printf(
        "core-loader bake: graph=absent IBL=%.6f lightmap=%.6f expected=pi captures=complete\n",
        ibl_mean, lighting_mean);
    return true;
}
} // namespace

int main(int, char**)
{
    return exercise() ? 0 : 1;
}
