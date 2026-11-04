#include "scene_snapshot.hpp"

#include "resource_fixture.hpp"

#if defined(ENGINE_TEST_RESOURCE_GRAPH)
#include "plugins/runtime/resource_graph/resource_graph_system.hpp"
#endif

#include "runtime/engine_config.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/post_process_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/utils/mesh_utils.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <array>
#include <cstdio>
#include <limits>

namespace
{

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "render foundation test: %s\n", message);
    }
    return value;
}

engine::schema::MeshDataT quad()
{
    engine::schema::MeshDataT result;
    result.path = "meshes/quad";
    result.vertices = {
        {-1, -1, 0, 0, 0, 1}, {1, -1, 0, 0, 0, 1}, {1, 1, 0, 0, 0, 1}, {-1, 1, 0, 0, 0, 1}};
    result.indices = {0, 1, 2, 0, 2, 3};
    result.texcoords = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    result.tangents.assign(4, {1, 0, 0, 1});
    result.colors.assign(4, {1, 1, 1, 1});
    result.submeshes = {{0, 3, 0}, {3, 3, 1}};
    return result;
}

bool check_tangent_aliasing()
{
    auto mesh = quad();
    // Authored tangent intentionally differs from the frame generated from these UVs.
    mesh.tangents.assign(mesh.vertices.size(), {0, 2, 0, -1});
    std::vector<engine::schema::Vec4> separate;
    if (!expect(engine::MeshUtils::build_tangents(mesh, separate) &&
                    engine::MeshUtils::build_tangents(mesh, mesh.tangents),
                "build authored tangents with separate and in-place output"))
    {
        return false;
    }
    for (std::size_t i = 0; i < mesh.tangents.size(); ++i)
    {
        const auto& actual = mesh.tangents[i];
        const auto& expected = separate[i];
        if (!expect(actual.x() == expected.x() && actual.y() == expected.y() &&
                        actual.z() == expected.z() && actual.w() == expected.w() &&
                        actual.x() == 0 && actual.y() == 1 && actual.z() == 0 && actual.w() == -1,
                    "in-place tangent generation retains the authored direction and handedness"))
        {
            return false;
        }
    }
    mesh.vertices.front().mutate_nx(0);
    mesh.vertices.front().mutate_ny(0);
    mesh.vertices.front().mutate_nz(0);
    return expect(!engine::MeshUtils::build_tangents(mesh, mesh.tangents) && mesh.tangents.empty(),
                  "failed in-place tangent generation clears output");
}

bool check_camera_and_light(engine::Scene& scene)
{
    auto camera_node = scene.create_node("Camera");
    auto light_node = scene.create_node("Light");
    auto camera = camera_node ? camera_node.add_component<engine::Camera>() : engine::Camera{};
    auto light = light_node ? light_node.add_component<engine::Light>() : engine::Light{};
    if (!expect(camera && light, "create camera and light as node components"))
    {
        return false;
    }
    auto camera_settings = camera.settings();
    camera_settings.projection = engine::schema::Projection::orthographic;
    camera_settings.orthographic_size = 4;
    if (!expect(camera.set_settings(camera_settings), "accept orthographic projection"))
    {
        return false;
    }
    auto invalid_camera = camera_settings;
    invalid_camera.orthographic_size = 0;
    if (!expect(!camera.set_settings(invalid_camera) && camera.settings() == camera_settings,
                "invalid projection leaves previous camera settings intact"))
    {
        return false;
    }
    invalid_camera = camera_settings;
    invalid_camera.projection = static_cast<engine::schema::Projection>(255);
    if (!expect(!camera.set_settings(invalid_camera), "reject unknown projection kind"))
    {
        return false;
    }

    auto light_settings = light.settings();
    for (const auto kind : {engine::schema::LightKind::directional,
                            engine::schema::LightKind::point, engine::schema::LightKind::spot})
    {
        light_settings.kind = kind;
        light_settings.range = 12;
        light_settings.inner_angle = 15;
        light_settings.outer_angle = 35;
        if (!expect(light.set_settings(light_settings), "accept supported light kind"))
        {
            return false;
        }
    }
    auto invalid_light = light_settings;
    invalid_light.range = -1;
    if (!expect(!light.set_settings(invalid_light) && light.settings() == light_settings,
                "invalid light range preserves previous settings"))
    {
        return false;
    }
    invalid_light = light_settings;
    invalid_light.inner_angle = invalid_light.outer_angle + 1;
    if (!expect(!light.set_settings(invalid_light), "reject inverted spotlight cone") ||
        !expect(scene.set_active_camera(camera), "set active orthographic camera"))
    {
        return false;
    }
    engine::schema::SceneDataT snapshot;
    if (!expect(scene.build_data(snapshot), "serialize camera and light generated data"))
    {
        return false;
    }
    bool found_camera = false;
    bool found_light = false;
    for (const auto& node : snapshot.nodes)
    {
        for (const auto& component : node->components)
        {
            if (const auto* value = component->payload.AsCameraData())
            {
                found_camera = *value == camera_settings;
            }
            if (const auto* value = component->payload.AsLightData())
            {
                found_light = *value == light_settings;
            }
        }
    }
    return expect(found_camera && found_light, "snapshot keeps projection and full light data");
}

bool check_scene_render_settings(const engine::EngineConfig& config)
{
    engine::WorldSystem world;
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows, &world);
    const auto initial = renderer.feature_state(engine::RenderFeature::frustum_culling);
    if (!expect(!initial.requested && !initial.supported && !initial.effective &&
                    initial.reason == engine::RenderFeatureReason::device_unavailable &&
                    renderer.submission_stats().empty(),
                "uninitialized feature queries report unavailable device") ||
        !expect(!renderer.set_debug_feature(engine::RenderFeature::wireframe, true) &&
                    !renderer.feature_state(engine::RenderFeature::wireframe).requested &&
                    !renderer.set_debug_feature(static_cast<engine::RenderFeature>(255), true),
                "unsupported or unknown requests preserve existing feature state") ||
        !expect(!renderer.set_debug_feature(engine::RenderFeature::frustum_culling, false) &&
                    !renderer.set_debug_feature(engine::RenderFeature::static_instancing, true) &&
                    !renderer.set_debug_feature(engine::RenderFeature::static_lod, true),
                "runtime component properties cannot be written through temporary debug controls"))
    {
        return false;
    }
    renderer.set_stats_visible(true);
    if (!expect(renderer.stats_visible() &&
                    renderer.feature_state(engine::RenderFeature::stats).requested &&
                    renderer.set_debug_feature(engine::RenderFeature::stats, false) &&
                    !renderer.stats_visible(),
                "legacy stats and unified feature entry share one state"))
    {
        return false;
    }
    const engine::DebugLine line{{0, 0, 0}, {1, 1, 1}, 0xffffffff, true};
    if (!expect(!renderer.draw_debug_lines(std::span(&line, 1)),
                "debug lines reject an unavailable renderer without retaining work"))
    {
        return false;
    }
    if (!world.initialize(config))
    {
        return false;
    }
    constexpr std::uint32_t idle_clear_color = 0x203040ff;
    renderer.set_idle_clear_color(idle_clear_color);
    if (!expect(renderer.settings().clear_color == idle_clear_color &&
                    renderer.settings().exposure == 1.0F &&
                    renderer.settings().ambient_intensity == 0.0F,
                "idle rendering keeps only a clear color and fixed effect defaults"))
    {
        return false;
    }
    auto* scene = world.create_scene("Environment ownership");
    if (!expect(scene != nullptr && renderer.settings().exposure == 1.0F &&
                    renderer.settings().ambient_intensity == 0.0F &&
                    renderer.settings().clear_color == engine::RenderSettings{}.clear_color,
                "a new scene never inherits idle or preceding-scene settings"))
    {
        return false;
    }
    auto node = scene->create_node("Environment");
    auto environment = node.valid() ? node.add_component<engine::EnvironmentComponent>()
                                    : engine::EnvironmentComponent{};
    engine::PostProcessSettings changed;
    changed.exposure = 2.0F;
    changed.ao_enabled = true;
    changed.ao_radius = 1.8F;
    changed.ao_strength = 0.8F;
    changed.ao_power = 1.4F;
    changed.ao_fade_start = 50.0F;
    changed.ao_fade_end = 120.0F;
    changed.hdr_enabled = true;
    changed.bloom_enabled = true;
    changed.bloom_threshold = 1.5F;
    changed.bloom_intensity = 0.4F;
    changed.bloom_radius = 1.2F;
    changed.ssr_enabled = true;
    changed.ssr_max_steps = 128;
    engine::EnvironmentSettings environment_settings;
    environment_settings.ambient_intensity = 0.3F;
    if (!expect(environment.valid() && environment.set_settings(environment_settings) &&
                    renderer.settings().ambient_intensity == 0.3F &&
                    renderer.settings().exposure == 1.0F,
                "environment edits are independent of missing post-process components"))
    {
        return false;
    }
    auto camera_node = scene->create_node("Camera");
    auto camera =
        camera_node.valid() ? camera_node.add_component<engine::Camera>() : engine::Camera{};
    auto post_process = camera_node.valid()
                            ? camera_node.add_component<engine::PostProcessComponent>()
                            : engine::PostProcessComponent{};
    if (!expect(
            camera.valid() && post_process.valid() && scene->set_active_camera(camera) &&
                post_process.set_settings(changed) &&
                environment.settings().ambient_intensity == 0.3F &&
                post_process.settings().exposure == 2.0F && renderer.settings().ao_enabled &&
                post_process.settings().ao_radius == 1.8F &&
                renderer.settings().ao_strength == 0.8F && renderer.settings().ao_power == 1.4F &&
                renderer.settings().ao_fade_start == 50.0F &&
                renderer.settings().ao_fade_end == 120.0F && renderer.settings().hdr_enabled &&
                renderer.settings().bloom_enabled && renderer.settings().bloom_threshold == 1.5F &&
                renderer.settings().bloom_intensity == 0.4F &&
                renderer.settings().bloom_radius == 1.2F && renderer.settings().ssr_enabled &&
                post_process.settings().ssr_max_steps == 128,
            "render snapshots read the current scene's generated component data"))
    {
        return false;
    }
    auto pending_ibl = environment.settings();
    auto invalid_ao = post_process.settings();
    auto invalid_hdr = post_process.settings();
    invalid_hdr.hdr_enabled = false;
    if (!expect(!post_process.set_settings(invalid_hdr) && renderer.settings().hdr_enabled &&
                    renderer.settings().bloom_enabled,
                "Bloom requires HDR and rejection preserves the entire post-process state"))
    {
        return false;
    }
    invalid_ao.ao_radius = 0.0F;
    if (!expect(!post_process.set_settings(invalid_ao) && renderer.settings().ao_radius == 1.8F,
                "zero AO radius is rejected before component changes"))
    {
        return false;
    }
    invalid_ao = post_process.settings();
    invalid_ao.ao_fade_end = invalid_ao.ao_fade_start;
    if (!expect(!post_process.set_settings(invalid_ao) && renderer.settings().ao_fade_end == 120.0F,
                "inverted AO fade is rejected atomically"))
    {
        return false;
    }
    pending_ibl.ibl_enabled = true;
    pending_ibl.ibl_intensity = 0.75F;
    if (!expect(environment.set_settings(pending_ibl) && renderer.settings().ibl_enabled &&
                    renderer.settings().ibl_intensity == 0.75F,
                "IBL can be configured while all three bake outputs are still absent"))
    {
        return false;
    }
    auto partial_ibl = pending_ibl;
    partial_ibl.ibl_irradiance_guid = "11111111-1111-1111-1111-111111111111";
    if (!expect(!environment.set_settings(partial_ibl) &&
                    environment.settings().ibl_irradiance_guid.empty(),
                "a partial IBL result is rejected without publishing incomplete references"))
    {
        return false;
    }
    engine::WorldSystem observer_world;
    if (!observer_world.initialize(config))
    {
        return false;
    }
    auto* observer_scene = observer_world.create_scene("Independent observation");
    auto observer_node =
        observer_scene != nullptr ? observer_scene->create_node("Observer") : engine::Node{};
    auto observer =
        observer_node.valid() ? observer_node.add_component<engine::Camera>() : engine::Camera{};
    auto observer_effects = observer_node.valid()
                                ? observer_node.add_component<engine::PostProcessComponent>()
                                : engine::PostProcessComponent{};
    engine::PostProcessSettings observer_settings;
    observer_settings.exposure = 7.0F;
    if (!expect(observer.valid() && observer_effects.valid() &&
                    observer_scene->set_active_camera(observer) &&
                    observer_effects.set_settings(observer_settings) &&
                    renderer.settings().exposure == 2.0F,
                "an independent observation camera does not supply the world's post-process"))
    {
        return false;
    }
    auto duplicate_node = scene->create_node("Duplicate environment");
    auto duplicate = duplicate_node.valid()
                         ? duplicate_node.add_component<engine::EnvironmentComponent>()
                         : engine::EnvironmentComponent{};
    if (!expect(duplicate.valid() && renderer.settings().ambient_intensity == 0.0F &&
                    environment.settings().ambient_intensity == 0.3F &&
                    post_process.settings().exposure == 2.0F,
                "ambiguous environments return fixed defaults without mutating components"))
    {
        return false;
    }
    if (!duplicate.set_enabled(false))
    {
        std::abort();
    }
    auto first_light_node = scene->create_node("Fill direction");
    auto second_light_node = scene->create_node("Shadow direction");
    auto first_light = first_light_node.valid() ? first_light_node.add_component<engine::Light>()
                                                : engine::Light{};
    auto second_light = second_light_node.valid() ? second_light_node.add_component<engine::Light>()
                                                  : engine::Light{};
    engine::LightSettings shadows;
    shadows.cast_shadows = true;
    shadows.shadow_resolution = 512;
    shadows.shadow_bias = 0.004F;
    shadows.shadow_cascades = 2;
    shadows.shadow_distance = 80.0F;
    shadows.shadow_split_lambda = 0.7F;
    shadows.shadow_cascade_blend = 0.15F;
    if (!expect(first_light.valid() && second_light.valid() && second_light.set_settings(shadows) &&
                    renderer.settings().directional_shadows &&
                    renderer.settings().shadow_resolution == 512 &&
                    renderer.settings().shadow_cascades == 2 &&
                    renderer.settings().shadow_distance == 80.0F &&
                    renderer.settings().shadow_split_lambda == 0.7F &&
                    renderer.settings().shadow_cascade_blend == 0.15F,
                "shadow settings follow the actual casting light, not the first fill light"))
    {
        return false;
    }
    auto parent = scene->create_node("Render state parent");
    if (!expect(parent.valid() && node.set_parent(parent) && camera_node.set_parent(parent) &&
                    second_light_node.set_parent(parent),
                "group scene render components under one parent"))
    {
        return false;
    }
    if (!parent.set_enabled(false))
    {
        std::abort();
    }
    if (!expect(renderer.settings().ambient_intensity == 0.0F &&
                    renderer.settings().exposure == 1.0F &&
                    !renderer.settings().directional_shadows && environment.enabled() &&
                    post_process.enabled() && second_light.enabled(),
                "disabled ancestors suppress rendering without changing component local states"))
    {
        return false;
    }
    if (!parent.set_enabled(true))
    {
        std::abort();
    }
    if (!parent.set_visible(false))
    {
        std::abort();
    }
    if (!expect(environment.active() && post_process.active() && second_light.active() &&
                    renderer.settings().ambient_intensity == 0.0F &&
                    renderer.settings().exposure == 1.0F &&
                    !renderer.settings().directional_shadows,
                "hidden ancestors suppress scene effects while keeping components active"))
    {
        return false;
    }
    if (!parent.set_visible(true))
    {
        std::abort();
    }
    if (!environment.set_visible(false))
    {
        std::abort();
    }
    if (!expect(renderer.settings().ambient_intensity == 0.0F &&
                    renderer.settings().exposure == 2.0F && renderer.settings().directional_shadows,
                "component visibility only removes that component's frame contribution"))
    {
        return false;
    }
    if (!environment.set_visible(true))
    {
        std::abort();
    }
    if (!expect(renderer.settings().ambient_intensity == 0.3F,
                "restoring hierarchy visibility restores the saved component parameters"))
    {
        return false;
    }
    auto changed_shadows = second_light.settings();
    changed_shadows.shadow_cascades = 4;
    changed_shadows.shadow_distance = 160.0F;
    if (!expect(second_light.set_settings(changed_shadows) &&
                    renderer.settings().shadow_cascades == 4 &&
                    renderer.settings().shadow_distance == 160.0F &&
                    !first_light.settings().cast_shadows,
                "cascade snapshots follow the explicitly edited casting light"))
    {
        return false;
    }
    changed_shadows.cast_shadows = false;
    if (!expect(second_light.set_settings(changed_shadows) &&
                    !renderer.settings().directional_shadows,
                "disabling a light's shadows updates the frame snapshot"))
    {
        return false;
    }
    if (!first_light.set_settings(shadows) || !second_light.set_settings(shadows) ||
        !expect(!renderer.settings().directional_shadows &&
                    environment.settings().ambient_intensity == 0.3F &&
                    first_light.settings().cast_shadows && second_light.settings().cast_shadows,
                "ambiguous shadow casters return defaults without modifying either light"))
    {
        return false;
    }
    if (!first_light.set_enabled(false))
    {
        std::abort();
    }
    if (!second_light.set_enabled(false))
    {
        std::abort();
    }
    if (!post_process.set_enabled(false))
    {
        std::abort();
    }
    if (!environment.set_enabled(false))
    {
        std::abort();
    }
    if (!expect(renderer.settings().exposure == 1.0F &&
                    renderer.settings().ambient_intensity == 0.0F &&
                    !renderer.settings().directional_shadows,
                "disabling scene effect components immediately restores fixed engine defaults"))
    {
        return false;
    }
    world.destroy_scene();
    if (!expect(renderer.settings().exposure == 1.0F &&
                    renderer.settings().ambient_intensity == 0.0F &&
                    renderer.settings().clear_color == idle_clear_color,
                "stopping the scene returns to independent idle defaults"))
    {
        return false;
    }
    return expect(world.create_scene("Empty successor") != nullptr &&
                      renderer.settings().exposure == 1.0F &&
                      renderer.settings().ambient_intensity == 0.0F,
                  "switching scenes does not preserve the previous scene's effects");
}

bool check_ecs_handle_lifetime(engine::Scene& scene)
{
    auto node = scene.create_node("ECS material owner");
    auto renderer = node.add_component<engine::MeshRenderer>();
    auto material = renderer.material();
    const auto renderer_identity = renderer.instance_id();
    const auto material_identity = material.instance_id();
    const engine::Vec4 color{0.25, 0.5, 0.75, 1.0};
    if (!expect(material && material.set_v4("baseColor", color),
                "configure material before ECS table changes"))
    {
        return false;
    }
    auto camera = node.add_component<engine::Camera>();
    const std::array<std::uint64_t, 1> hidden{renderer_identity};
    if (!expect(camera && camera.set_hidden_meshes(hidden) && node.add_component<engine::Light>() &&
                    node.remove_component(engine::Light::component_type),
                "move retained components between ECS tables"))
    {
        return false;
    }
    engine::Vec4 current;
    if (!expect(renderer.instance_id() == renderer_identity &&
                    material.instance_id() == material_identity &&
                    material.get_v4("baseColor", current) && current == color &&
                    !camera.mesh_visible(renderer_identity),
                "value handles preserve material and camera runtime state across table moves"))
    {
        return false;
    }
    engine::schema::ComponentDataT before;
    engine::schema::ComponentDataT after;
    if (!renderer.build_data(before))
    {
        return false;
    }
    auto invalid = before;
    invalid.flags.reset();
    invalid.payload.AsMeshRendererData()->cast_shadows = !renderer.cast_shadows();
    if (!expect(!renderer.restore_data(invalid) && renderer.build_data(after) && before == after &&
                    material.instance_id() == material_identity &&
                    material.get_v4("baseColor", current) && current == color,
                "typed restore rejects missing flags before touching settings or material state") ||
        !renderer.set_material_count(3))
    {
        return false;
    }
    auto removed_slot = renderer.material(2);
    const auto removed_identity = removed_slot.instance_id();
    if (!expect(renderer.set_material_count(1) && !removed_slot && renderer.set_material_count(3) &&
                    !removed_slot && renderer.material(2).instance_id() != removed_identity &&
                    material && material.get_v4("baseColor", current) && current == color,
                "material slot reallocation preserves retained handles and never revives removed "
                "ones") ||
        !expect(renderer.restore_data(before) && !material && renderer.material() &&
                    renderer.material().instance_id() != material_identity,
                "successful material restoration invalidates previous slot handles"))
    {
        return false;
    }
    auto restored_material = renderer.material();
    if (!expect(node.remove_component(engine::MeshRenderer::component_type) && !renderer &&
                    !restored_material && node.add_component<engine::MeshRenderer>() && !renderer &&
                    !restored_material,
                "component removal and replacement never revive old render or material handles"))
    {
        return false;
    }
    return expect(scene.destroy_node(node.id()) && !node && !camera,
                  "deleting the entity invalidates every retained component handle");
}

bool check_mesh(engine::Scene& scene)
{
    auto node = scene.create_node("Editable mesh");
    auto renderer = node ? node.add_component<engine::MeshRenderer>() : engine::MeshRenderer{};
    if (!expect(renderer && renderer.material_count() == 1,
                "mesh starts with the compatible first material slot") ||
        !expect(renderer.set_material_count(2) && renderer.material(1).valid() &&
                    renderer.material(2).valid() == false,
                "material slots have explicit bounds") ||
        !expect(!renderer.set_material_count(0) && !renderer.set_material_count(17),
                "material slot count stays within supported bounds"))
    {
        return false;
    }
    auto data = quad();
    const auto old_revision = renderer.mesh_revision();
    if (!expect(renderer.set_mesh_data(data) && renderer.mesh_revision() > old_revision,
                "dynamic mesh submission advances its revision") ||
        !expect(renderer.mesh_data() && *renderer.mesh_data() == data,
                "dynamic mesh preserves UV, tangent, color and submesh data"))
    {
        return false;
    }
    const auto revision = renderer.mesh_revision();
    data.vertices.front().mutate_x(-2);
    if (!expect(renderer.mesh_data()->vertices.front().x() == -1,
                "submitted mesh owns an independent copy of caller data"))
    {
        return false;
    }
    auto invalid = data;
    invalid.indices.back() = 99;
    if (!expect(!renderer.set_mesh_data(invalid) && renderer.mesh_revision() == revision,
                "out-of-range indices reject the complete update"))
    {
        return false;
    }
    invalid = data;
    invalid.texcoords.pop_back();
    if (!expect(!renderer.set_mesh_data(invalid), "reject incomplete optional vertex channel"))
    {
        return false;
    }
    invalid = data;
    invalid.vertices.front().mutate_x(std::numeric_limits<float>::infinity());
    if (!expect(!renderer.set_mesh_data(invalid), "reject non-finite positions"))
    {
        return false;
    }
    invalid = data;
    invalid.submeshes = {{0, 3, 0}, {0, 3, 1}};
    return expect(!renderer.set_mesh_data(invalid), "reject overlapping submesh ranges") &&
           expect(renderer.set_mesh_data(data) && renderer.mesh_revision() > revision,
                  "valid geometry update recovers after rejected data") &&
           expect(!renderer.set_material_count(1),
                  "cannot remove material slots referenced by the active mesh") &&
           expect(renderer.set_mesh("builtin://meshes/cube") && renderer.mesh_data() != nullptr,
                  "resource mesh can replace dynamic geometry") &&
           expect(renderer.set_material_count(1) && renderer.material(1).valid() == false,
                  "removing an unused material slot invalidates its lookup");
}

bool check_deformation(engine::Scene& scene)
{
    auto node = scene.create_node("Deformed mesh");
    auto renderer = node ? node.add_component<engine::MeshRenderer>() : engine::MeshRenderer{};
    if (!expect(renderer && renderer.set_material_count(2), "create deformation owner"))
    {
        return false;
    }
    const auto* source = renderer.mesh_data();
    const auto original = *source;
    const auto source_revision = renderer.mesh_revision();
    const auto source_instance = renderer.instance_id();
    engine::schema::ComponentDataT snapshot;
    std::vector<engine::Vec4> tangents;
    auto vertices = source->vertices;
    if (!expect(renderer.build_data(snapshot) &&
                    engine::MeshUtils::build_tangents(*source, tangents),
                "capture bind geometry and persistent identity"))
    {
        return false;
    }
    for (auto& vertex : vertices)
    {
        vertex.mutate_x(vertex.x() + 3.0F);
    }
    if (!expect(renderer.set_deformation(vertices, tangents), "accept complete deformed frame"))
    {
        return false;
    }
    const auto revision = renderer.deformation_revision();
    const auto first_x = renderer.deformed_vertices().front().x();
    vertices.front().mutate_x(50.0F);
    tangents.front().mutate_w(-tangents.front().w());
    engine::schema::ComponentDataT after;
    if (!expect(renderer.deformed_vertices().front().x() == first_x &&
                    renderer.deformed_tangents().front().w() != tangents.front().w(),
                "deformation owns caller-independent vertex and tangent copies") ||
        !expect(renderer.mesh_data() == source && *renderer.mesh_data() == original &&
                    renderer.mesh_revision() == source_revision &&
                    renderer.mesh_path() == "builtin://meshes/cube" &&
                    renderer.material_count() == 2,
                "deformation preserves bind data, GUID ownership and material slots") ||
        !expect(renderer.build_data(after) && after == snapshot,
                "persistent snapshot excludes the transient pose"))
    {
        return false;
    }
    auto invalid = vertices;
    invalid.pop_back();
    if (!expect(!renderer.set_deformation(invalid, tangents) &&
                    !renderer.set_deformation(vertices, {}),
                "reject mismatched deformation streams"))
    {
        return false;
    }
    invalid = vertices;
    invalid.front().mutate_x(std::numeric_limits<float>::quiet_NaN());
    if (!expect(!renderer.set_deformation(invalid, tangents), "reject nonfinite pose"))
    {
        return false;
    }
    invalid = vertices;
    invalid.front().mutate_nx(0);
    invalid.front().mutate_ny(0);
    invalid.front().mutate_nz(0);
    auto invalid_tangents = tangents;
    invalid_tangents.front().mutate_w(0);
    if (!expect(!renderer.set_deformation(invalid, tangents) &&
                    !renderer.set_deformation(vertices, invalid_tangents),
                "reject zero normals and invalid tangent handedness"))
    {
        return false;
    }
    invalid_tangents = tangents;
    invalid_tangents.front() = {vertices.front().nx(), vertices.front().ny(), vertices.front().nz(),
                                1};
    if (!expect(!renderer.set_deformation(vertices, invalid_tangents) &&
                    renderer.deformation_revision() == revision &&
                    renderer.deformed_vertices().front().x() == first_x,
                "invalid tangent frame leaves the last complete pose intact") ||
        !expect(!renderer.set_mesh("missing.mesh") && renderer.deformation_revision() == revision &&
                    !renderer.deformed_vertices().empty(),
                "failed source change retains deformation"))
    {
        return false;
    }
    renderer.clear_deformation();
    const auto cleared_revision = renderer.deformation_revision();
    renderer.clear_deformation();
    if (!expect(renderer.deformed_vertices().empty() && renderer.deformed_tangents().empty() &&
                    cleared_revision > revision &&
                    renderer.deformation_revision() == cleared_revision &&
                    renderer.mesh_data() == source,
                "clear restores source geometry and is repeatable") ||
        !expect(renderer.set_deformation(vertices, tangents) && renderer.restore_data(snapshot) &&
                    renderer.deformed_vertices().empty(),
                "restoring persistent data discards transient pose") ||
        !expect(renderer.set_deformation(vertices, tangents) &&
                    renderer.set_mesh("builtin://meshes/cube") &&
                    renderer.deformed_vertices().empty(),
                "successful source replacement discards transient pose"))
    {
        return false;
    }
    if (!expect(renderer.set_deformation(vertices, tangents) && renderer.set_mesh_data(quad()) &&
                    renderer.deformed_vertices().empty() &&
                    renderer.instance_id() == source_instance,
                "dynamic topology replacement discards pose and retains instance identity") ||
        !node.remove_component(engine::MeshRenderer::component_type))
    {
        return false;
    }
    renderer = node.add_component<engine::MeshRenderer>();
    return expect(renderer.valid() && renderer.instance_id() != 0 &&
                      renderer.instance_id() != source_instance,
                  "replacement component has distinct identity even on the same node");
}

bool check_skinning_palette(engine::Scene& scene)
{
    auto node = scene.create_node("GPU pose owner");
    auto renderer = node ? node.add_component<engine::MeshRenderer>() : engine::MeshRenderer{};
    const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::array<std::array<float, 16>, 1> positions{identity};
    std::array<std::array<float, 16>, 1> normals{identity};
    if (!expect(renderer && renderer.mesh_resource() &&
                    !renderer.set_skinning_palette(positions, normals),
                "immutable source ownership is exposed but unskinned meshes reject palettes"))
    {
        return false;
    }
    auto mesh = quad();
    mesh.skin.assign(mesh.vertices.size(), {0, 0, 0, 0, 1, 0, 0, 0});
    if (!expect(renderer.set_mesh_data(mesh) && !renderer.mesh_resource() &&
                    renderer.set_skinning_palette(positions, normals),
                "dynamic skinned mesh accepts a complete independent palette"))
    {
        return false;
    }
    const auto revision = renderer.deformation_revision();
    positions[0][12] = 5;
    normals[0][0] = 0.5F;
    if (!expect(renderer.skinning_palette()[0][12] == 0 &&
                    renderer.skinning_normal_palette()[0][0] == 1,
                "palette upload copies both streams instead of borrowing caller memory"))
    {
        return false;
    }
    auto invalid = positions;
    invalid[0][0] = std::numeric_limits<float>::infinity();
    if (!expect(!renderer.set_skinning_palette(invalid, normals) &&
                    !renderer.set_skinning_palette({}, {}) &&
                    !renderer.set_skinning_palette(positions, {}) &&
                    renderer.deformation_revision() == revision,
                "invalid or incomplete palette submissions preserve the previous pose"))
    {
        return false;
    }
    invalid = positions;
    invalid[0][3] = 1;
    if (!expect(!renderer.set_skinning_palette(invalid, normals) &&
                    renderer.set_deformation(mesh.vertices, mesh.tangents) &&
                    renderer.skinning_palette().empty() &&
                    renderer.set_skinning_palette(positions, normals) &&
                    renderer.deformed_vertices().empty(),
                "affine validation and atomic switching keep one deformation source"))
    {
        return false;
    }
    renderer.clear_deformation();
    const auto cleared = renderer.deformation_revision();
    renderer.clear_deformation();
    return expect(
        renderer.skinning_palette().empty() && renderer.skinning_normal_palette().empty() &&
            renderer.deformation_revision() == cleared &&
            renderer.set_skinning_palette(positions, normals) &&
            renderer.set_mesh("builtin://meshes/cube") && renderer.skinning_palette().empty(),
        "clear is repeatable and source replacement discards the old GPU pose");
}

bool check_textures(engine::ResourceSystem& resources, engine::Scene& scene, bool shared)
{
    test_support::ResourceFixture fixture;
    engine::schema::TextureDataT texture;
    texture.path = "textures/gray";
    texture.width = 1;
    texture.height = 1;
    texture.encoding = engine::schema::TextureEncoding::rgba8;
    texture.format = engine::schema::TextureFormat::rgba8;
    texture.data = {128, 128, 128, 255};
    if (!expect(fixture.add(texture) && fixture.publish(resources),
                "publish a texture through the GUID resource catalog"))
    {
        return false;
    }
    auto first_node = scene.create_node("First textured mesh");
    auto second_node = scene.create_node("Second textured mesh");
    auto first =
        first_node ? first_node.add_component<engine::MeshRenderer>() : engine::MeshRenderer{};
    auto second =
        second_node ? second_node.add_component<engine::MeshRenderer>() : engine::MeshRenderer{};
    if (!expect(first && second, "create texture owners") ||
        !expect(first.material().set_texture("baseColorTexture", texture.path) &&
                    second.material().set_texture("baseColorTexture", texture.path),
                "resolve texture by named shader sampler"))
    {
        return false;
    }
    auto first_texture = first.material().texture("baseColorTexture");
    auto second_texture = second.material().texture("baseColorTexture");
    std::weak_ptr<const engine::Resource> first_held = first_texture;
    std::weak_ptr<const engine::Resource> second_held = second_texture;
    std::string_view path;
    if (!expect(first_texture && second_texture && first_texture->texture() &&
                    second_texture->texture() && (first_texture == second_texture) == shared &&
                    *first_texture->texture() == *second_texture->texture(),
                "core loads distinct texture entities; injected graph shares matching entities") ||
        !expect(first.material().get_texture("baseColorTexture", path) && path == texture.path,
                "query returns the resolved texture path") ||
        !expect(!first.material().set_texture("missingSampler", texture.path) &&
                    !first.material().set_texture("baseColorTexture", "builtin://meshes/cube") &&
                    !first.material().set_texture("baseColorTexture", "missing.png") &&
                    first.material().get_texture("baseColorTexture", path) && path == texture.path,
                "invalid texture assignments preserve current resource"))
    {
        return false;
    }
    first_texture.reset();
    second_texture.reset();
    const auto first_id = first_node.id();
    const auto second_id = second_node.id();
    return expect(scene.destroy_node(first_id) && first_held.expired() != shared &&
                      !second_held.expired(),
                  "destroying a material releases only resources without remaining owners") &&
           expect(scene.destroy_node(second_id) && first_held.expired() && second_held.expired(),
                  "last material owner releases every loaded texture entity");
}

} // namespace

int main()
{
    const engine::EngineConfig config;
    engine::ResourceSystem resources;
#if defined(ENGINE_TEST_RESOURCE_GRAPH)
    engine::plugins::resource_graph::ResourceGraphSystem graph(resources);
#endif
    engine::WorldSystem world;
    if (!expect(resources.initialize(config) && world.initialize(config),
                "initialize resource and world systems"))
    {
        return 1;
    }
    auto* scene = world.create_scene("Render foundation");
    if (!expect(scene != nullptr, "create scene") || !check_tangent_aliasing() ||
        !check_camera_and_light(*scene) || !check_ecs_handle_lifetime(*scene) ||
        !check_mesh(*scene) || !check_deformation(*scene) || !check_skinning_palette(*scene) ||
        !check_textures(resources, *scene, false))
    {
        return 1;
    }
#if defined(ENGINE_TEST_RESOURCE_GRAPH)
    if (!expect(graph.initialize(config), "initialize optional dependency graph"))
    {
        return 1;
    }
    resources.set_loader(&graph);
    if (!check_textures(resources, *scene, true))
    {
        return 1;
    }
    resources.set_loader(nullptr);
#endif
    if (!check_scene_render_settings(config))
    {
        return 1;
    }
    std::puts("Render component validation, dynamic mesh ownership and texture sharing passed.");
    return 0;
}
