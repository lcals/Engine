#include "resource_fixture.hpp"

#include "plugins/runtime/resource_graph/resource_graph_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/render/render_constants.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/light_probe_volume_component.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/post_process_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
using namespace engine;

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "render submission: %s (SDL: %s)\n", message, SDL_GetError());
    }
    return value;
}

schema::MeshDataT grid()
{
    schema::MeshDataT mesh;
    mesh.path = "meshes/submission-grid";
    constexpr std::uint32_t width = 8;
    for (std::uint32_t y = 0; y <= width; ++y)
    {
        for (std::uint32_t x = 0; x <= width; ++x)
        {
            mesh.vertices.emplace_back(static_cast<float>(x) / width - .5F,
                                       static_cast<float>(y) / width - .5F, 0, 0, 0, 1);
        }
    }
    for (std::uint32_t y = 0; y < width; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const auto first = y * (width + 1) + x;
            mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + width + 2, first,
                                                     first + width + 2, first + width + 1});
        }
    }
    auto lod = std::make_unique<schema::MeshLodDataT>();
    lod->screen_height = .2F;
    lod->indices = {0, 8, 80, 0, 80, 72};
    lod->submeshes.emplace_back(0, 6, 0);
    mesh.lods.push_back(std::move(lod));
    return mesh;
}

schema::LightProbeDataT probes(const char* path, bool left)
{
    schema::LightProbeDataT data;
    data.path = path;
    data.bounds_min = {left ? -3.0 : 1.0, -2, -1};
    data.bounds_max = {left ? -1.0 : 3.0, 2, 1};
    data.grid_x = data.grid_y = data.grid_z = 2;
    data.baked_transform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    data.valid.assign(8, 1);
    data.coefficients.resize(8 * 9);
    for (std::size_t probe = 0; probe < 8; ++probe)
    {
        data.coefficients[probe * 9] =
            left ? schema::Vec3{.5, .125, .0625} : schema::Vec3{.0625, .125, .5};
    }
    data.source_fingerprint = "render-submission-probes";
    return data;
}

bool set_culling(Camera camera, bool enabled)
{
    auto settings = camera.settings();
    settings.frustum_culling_enabled = enabled;
    return camera.set_settings(settings);
}

bool set_instancing(const std::vector<MeshRenderer>& meshes, bool enabled)
{
    for (auto mesh : meshes)
    {
        if (!mesh.set_static_instancing_enabled(enabled))
        {
            return false;
        }
    }
    return true;
}

bool set_lod(const std::vector<MeshRenderer>& meshes, bool enabled)
{
    for (auto mesh : meshes)
    {
        if (!mesh.set_static_lod_enabled(enabled))
        {
            return false;
        }
    }
    return true;
}

bool frames(WindowSystem& window, RenderSystem& renderer, int count)
{
    int submitted = 0;
    for (int attempt = 0; attempt < 120 && submitted < count; ++attempt)
    {
        if (!window.tick(1.0 / 60.0) || window.exit_requested())
        {
            return false;
        }
        if (!window.can_render())
        {
            SDL_Delay(5);
            continue;
        }
        if (!renderer.render())
        {
            return false;
        }
        ++submitted;
    }
    return submitted == count;
}

bool capture(WindowSystem& window, RenderSystem& renderer, SceneCaptureImage& image,
             RenderViewStats& stats, bool exhaust_instances = false)
{
    if (!frames(window, renderer, 2) || !renderer.request_hdr_capture())
    {
        return false;
    }
    if (exhaust_instances)
    {
        // Reserve the actual upstream transient pool before the scene draw. No test-only
        // production visibility or fake availability hooks are needed.
        constexpr std::uint16_t stride = 7 * 4 * sizeof(float);
        std::uint64_t reserved = 0;
        while (const auto available = bgfx::getAvailInstanceDataBuffer(4096, stride))
        {
            bgfx::InstanceDataBuffer reservation;
            bgfx::allocInstanceDataBuffer(&reservation, available, stride);
            reserved += available;
            if (!expect(reserved * stride <= bgfx::getCaps()->limits.maxTransientVbSize,
                        "transient reservations remain within the device's finite buffer budget"))
            {
                return false;
            }
        }
        if (!expect(reserved > 0, "instance pool has capacity before explicit exhaustion"))
        {
            return false;
        }
        if (!expect(bgfx::getAvailInstanceDataBuffer(2, stride) < 2,
                    "the real transient instance pool is exhausted"))
        {
            return false;
        }
    }
    for (int attempt = 0;
         attempt < 64 && renderer.hdr_capture_status() == SceneCaptureStatus::pending; ++attempt)
    {
        if (!frames(window, renderer, 1))
        {
            return false;
        }
        if (attempt == 0)
        {
            const auto views = renderer.submission_stats();
            const auto found = std::find_if(views.begin(), views.end(), [](const auto& view)
                                            { return view.view_id == render_detail::main_view; });
            if (found == views.end())
            {
                return false;
            }
            stats = *found;
        }
    }
    if (!renderer.take_hdr_capture(image))
    {
        return false;
    }
    return true;
}

bool pixels(const SceneCaptureImage& left, const SceneCaptureImage& right, const char* message)
{
    if (!expect(left.width == right.width && left.height == right.height &&
                    left.rgba.size() == right.rgba.size() && !left.rgba.empty(),
                "capture dimensions are identical"))
    {
        return false;
    }
    float maximum = 0;
    for (std::size_t index = 0; index < left.rgba.size(); ++index)
    {
        if (!std::isfinite(left.rgba[index]) || !std::isfinite(right.rgba[index]))
        {
            return expect(false, "capture must contain finite values");
        }
        maximum = std::max(maximum, std::abs(left.rgba[index] - right.rgba[index]));
    }
    std::printf("render submission comparison: %s, max linear HDR difference %.9g\n", message,
                maximum);
    // Integer-aligned binary-exact geometry isolates submission changes, not raster placement.
    return expect(maximum == 0, message);
}

int test()
{
    EngineConfig config;
    config.window_title = "Upstream rendering submission regression";
    config.width = config.height = 256;
    ResourceSystem resources;
    plugins::resource_graph::ResourceGraphSystem graph(resources);
    WorldSystem world;
    WindowSystem window;
    RenderSystem renderer(window, &world);
    test_support::ResourceFixture fixture;
    const auto geometry = grid();
    const auto left_probes = probes("lighting/submission-left", true);
    const auto right_probes = probes("lighting/submission-right", false);
    if (!resources.initialize(config) || !graph.initialize(config) || !world.initialize(config) ||
        !window.initialize(config) || !renderer.initialize(config) || !fixture.add(geometry) ||
        !fixture.add(left_probes) || !fixture.add(right_probes) || !fixture.publish(resources))
    {
        return 1;
    }
    resources.set_loader(&graph);
    if (bgfx::getCaps()->limits.maxInstanceData < 7)
    {
        std::fprintf(stderr, "render submission: device has fewer than 7 instance-data slots\n");
        return 77;
    }
    auto* scene = world.create_scene("Submission paths");
    auto camera_node = scene ? scene->create_node("Camera") : Node{};
    auto camera = camera_node ? camera_node.add_component<Camera>() : Camera{};
    auto post =
        camera_node ? camera_node.add_component<PostProcessComponent>() : PostProcessComponent{};
    auto environment_node = scene ? scene->create_node("Environment") : Node{};
    auto environment = environment_node ? environment_node.add_component<EnvironmentComponent>()
                                        : EnvironmentComponent{};
    auto light_node = scene ? scene->create_node("Directional light") : Node{};
    auto light = light_node ? light_node.add_component<Light>() : Light{};
    CameraSettings lens;
    lens.projection = schema::Projection::orthographic;
    lens.orthographic_size = 4;
    lens.frustum_culling_enabled = false;
    TransformSettings camera_pose;
    camera_pose.position = {0, 0, 6};
    auto lighting = environment.settings();
    lighting.ambient_intensity = .3F;
    if (!camera || !post || !environment || !light || !camera.set_settings(lens) ||
        !camera_node.get_component<Transform>().set_settings(camera_pose) ||
        !scene->set_active_camera(camera) || !environment.set_settings(lighting))
    {
        return 1;
    }
    std::vector<MeshRenderer> visible;
    std::vector<MeshRenderer> meshes;
    for (int index = 0; index < 14; ++index)
    {
        auto node = scene->create_node("Mesh " + std::to_string(index));
        auto mesh = node ? node.add_component<MeshRenderer>() : MeshRenderer{};
        TransformSettings pose;
        pose.position = index < 6 ? Vec3{(index % 3 - 1) * 2.0, index / 3 == 0 ? -1.0 : 1.0, 0}
                                  : Vec3{20.0 + index, 0, 0};
        if (!mesh || !mesh.set_mesh(geometry.path) || !mesh.set_static_instancing_enabled(false) ||
            !mesh.set_static_lod_enabled(false) ||
            !node.get_component<Transform>().set_settings(pose) ||
            !mesh.material().set_type("builtin://materials/default-pbr") ||
            !mesh.material().set_v4("baseColor", {.2, .6, .9, 1}) ||
            !mesh.material().set_float("metallic", 0) || !mesh.material().set_float("roughness", 1))
        {
            return 1;
        }
        meshes.push_back(mesh);
        if (index < 6)
        {
            visible.push_back(mesh);
        }
    }
    SceneCaptureImage baseline, culled, instanced, changed_baseline, changed_instanced,
        lod_baseline, lod_instanced, exhausted, probe_baseline, probe_instanced, instance_opt_out,
        lod_opt_out;
    RenderViewStats all_stats, cull_stats, instance_stats, changed_stats, changed_instance_stats,
        lod_stats, lod_instance_stats, exhausted_stats, probe_stats, probe_instance_stats,
        instance_opt_out_stats, lod_opt_out_stats;
    if (!capture(window, renderer, baseline, all_stats) || !set_culling(camera, true) ||
        !capture(window, renderer, culled, cull_stats) ||
        !pixels(baseline, culled, "frustum culling preserves every pixel") ||
        !expect(all_stats.candidate_meshes == 14 && all_stats.culled_meshes == 0 &&
                    cull_stats.culled_meshes == 8 && cull_stats.visible_meshes == 6 &&
                    cull_stats.draw_calls < all_stats.draw_calls,
                "culling removes only the eight truly offscreen submissions") ||
        !set_instancing(meshes, true) || !capture(window, renderer, instanced, instance_stats) ||
        !pixels(culled, instanced, "shared opaque instances preserve every pixel") ||
        !expect(instance_stats.instance_batches > 0 &&
                    instance_stats.draw_calls < cull_stats.draw_calls &&
                    instance_stats.submitted_instances == 6 &&
                    instance_stats.triangles == cull_stats.triangles,
                "instances reduce draw calls without losing meshes or triangles"))
    {
        return 1;
    }
    if (!visible[0].set_static_instancing_enabled(false) ||
        !capture(window, renderer, instance_opt_out, instance_opt_out_stats) ||
        !pixels(culled, instance_opt_out,
                "one mesh can opt out without changing other instances") ||
        !expect(instance_opt_out_stats.draw_calls == 2 &&
                    instance_opt_out_stats.instance_batches == 1 &&
                    instance_opt_out_stats.submitted_instances == 6 &&
                    visible[1].static_instancing_enabled(),
                "one ordinary draw and one five-instance batch respect local mesh attributes") ||
        !visible[0].set_static_instancing_enabled(true))
    {
        return 1;
    }
    if (!capture(window, renderer, exhausted, exhausted_stats, true) ||
        !pixels(culled, exhausted,
                "exhausted transient instances fall back without losing pixels") ||
        !expect(exhausted_stats.instance_batches == 0 &&
                    exhausted_stats.draw_calls == cull_stats.draw_calls &&
                    exhausted_stats.submitted_instances == cull_stats.submitted_instances &&
                    exhausted_stats.triangles == cull_stats.triangles,
                "no transient capacity safely selects ordinary draws for every mesh"))
    {
        return 1;
    }
    auto mirrored = visible[1].node().get_component<Transform>().settings();
    mirrored.scale = {-1, 1, 1};
    if (!visible[0].material().set_v4("baseColor", {.9, .2, .1, 1}) ||
        !visible[1].node().get_component<Transform>().set_settings(mirrored) ||
        !set_instancing(meshes, false) ||
        !capture(window, renderer, changed_baseline, changed_stats) ||
        !set_instancing(meshes, true) ||
        !capture(window, renderer, changed_instanced, changed_instance_stats) ||
        !pixels(changed_baseline, changed_instanced,
                "material overrides and mirrored culling stay isolated") ||
        !expect(changed_instance_stats.draw_calls >= 3 &&
                    changed_instance_stats.instance_batches > 0,
                "material and mirrored winding differences split incompatible batches"))
    {
        return 1;
    }
    auto large = visible[0].node().get_component<Transform>().settings();
    large.scale = {2.5, 2.5, 1};
    if (!visible[0].material().set_v4("baseColor", {.2, .6, .9, 1}) ||
        !visible[0].node().get_component<Transform>().set_settings(large) ||
        !set_instancing(meshes, false) || !set_lod(meshes, true) ||
        !capture(window, renderer, lod_baseline, lod_stats) || !set_instancing(meshes, true) ||
        !capture(window, renderer, lod_instanced, lod_instance_stats) ||
        !pixels(lod_baseline, lod_instanced,
                "different LOD topology cannot be merged into one batch") ||
        !expect(lod_instance_stats.triangles == geometry.indices.size() / 3 + 5 * 2 &&
                    lod_stats.triangles == lod_instance_stats.triangles &&
                    lod_instance_stats.triangles < cull_stats.triangles &&
                    lod_instance_stats.draw_calls >= 3 && lod_instance_stats.instance_batches > 0,
                "one large LOD0 and five small LOD1 instances submit the exact expected triangle "
                "count"))
    {
        return 1;
    }
    if (!visible[2].set_static_lod_enabled(false) ||
        !capture(window, renderer, lod_opt_out, lod_opt_out_stats) ||
        !expect(lod_opt_out_stats.triangles == 2 * geometry.indices.size() / 3 + 4 * 2 &&
                    visible[3].static_lod_enabled(),
                "disabling LOD on one small mesh preserves its original topology only") ||
        !visible[2].set_static_lod_enabled(true))
    {
        return 1;
    }
    large.scale = {1, 1, 1};
    mirrored.scale = {1, 1, 1};
    if (!visible[0].node().get_component<Transform>().set_settings(large) ||
        !visible[1].node().get_component<Transform>().set_settings(mirrored) ||
        !set_lod(meshes, false))
    {
        return 1;
    }
    for (const auto* resource : {&left_probes, &right_probes})
    {
        auto node = scene->create_node(resource->path);
        auto volume = node.add_component<LightProbeVolumeComponent>();
        auto settings = volume.settings();
        if (!volume || !ResourceSystem::path_to_guid(resource->path, settings.probes_guid) ||
            !volume.set_settings(settings))
        {
            return 1;
        }
    }
    if (!set_instancing(meshes, false) || !capture(window, renderer, probe_baseline, probe_stats) ||
        !set_instancing(meshes, true) ||
        !capture(window, renderer, probe_instanced, probe_instance_stats) ||
        !pixels(probe_baseline, probe_instanced,
                "incompatible probe lighting bindings cannot share a batch") ||
        !expect(
            probe_instanced.rgba != culled.rgba && probe_instance_stats.instance_batches == 3 &&
                probe_instance_stats.draw_calls == 3 &&
                probe_instance_stats.submitted_instances == 6,
            "left probes, right probes and unbound meshes form three distinct lighting batches"))
    {
        return 1;
    }
    std::printf("render submission: draws %u -> %u -> %u; LOD triangles %llu -> %llu\n",
                all_stats.draw_calls, cull_stats.draw_calls, instance_stats.draw_calls,
                static_cast<unsigned long long>(cull_stats.triangles),
                static_cast<unsigned long long>(lod_instance_stats.triangles));
    renderer.shutdown();
    world.destroy_scene();
    return 0;
}
} // namespace

int main(int, char**)
{
    return test();
}
