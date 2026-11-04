#include "editor/commands/editor_command_channel.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/editor_edit.hpp"
#include "editor/commands/scene_commands.hpp"
#include "editor/render/render_commands.hpp"
#include "resource_fixture.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/light_probe_volume_component.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/planar_reflection_component.hpp"
#include "runtime/scene/post_process_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"
#include "scene_snapshot.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
using Json = nlohmann::json;
namespace fs = std::filesystem;

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "editor commands: %s\n", message);
    }
    return value;
}

Json target(const engine::Node& node)
{
    return {{"sceneId", std::to_string(node.scene()->id())}, {"nodeId", std::to_string(node.id())}};
}

bool same_texture(const engine::ResourcePtr& first, const engine::ResourcePtr& second)
{
    return first && second && first->info() == second->info() &&
           first->generation() == second->generation() && first->texture() && second->texture() &&
           *first->texture() == *second->texture();
}

bool scene_render_settings_tests()
{
    engine::WorldSystem world;
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows, &world);
    if (!world.initialize({}))
    {
        return false;
    }
    engine::editor::EditorCommands commands(
        [&world]() { return world.scene() == nullptr ? 0 : world.scene()->id(); });
    engine::editor::RenderCommands module(renderer, world, {});
    engine::editor::SceneCommands scene_commands(world);
    if (!module.register_commands(commands, scene_commands))
    {
        return false;
    }
    Json result;
    std::string error;
    const auto execute = [&](std::string_view name, const Json& arguments)
    {
        const bool success = commands.execute(name, arguments, result, error);
        if (!success && name != "set_render_settings")
        {
            std::fprintf(stderr, "scene render settings: %s\n", error.c_str());
        }
        return success;
    };
    const auto initial_revision = commands.mutation_revision();
    if (!expect(execute("set_render_settings", {{"clearColor", {0.0, 0.0, 0.0, 1.0}}}) &&
                    renderer.idle_clear_color() == 0x000000ff &&
                    !execute("set_render_settings", {{"ambientIntensity", .3}}) &&
                    renderer.settings().ambient_intensity == 0.0F &&
                    commands.mutation_revision() == initial_revision,
                "idle color does not create a scene or accept scene effect parameters"))
    {
        return false;
    }
    auto* scene = world.create_scene("Persistent scene settings");
    auto camera_node = scene == nullptr ? engine::Node{} : scene->create_node("Camera");
    auto camera = !camera_node ? engine::Camera{} : camera_node.add_component<engine::Camera>();
    auto first_node = scene == nullptr ? engine::Node{} : scene->create_node("Fill light");
    auto second_node = scene == nullptr ? engine::Node{} : scene->create_node("Shadow light");
    auto first = !first_node ? engine::Light{} : first_node.add_component<engine::Light>();
    auto second = !second_node ? engine::Light{} : second_node.add_component<engine::Light>();
    if (!camera || !first || !second || !scene->set_active_camera(camera))
    {
        return false;
    }
    auto shadow = second.settings();
    shadow.cast_shadows = true;
    shadow.shadow_resolution = 1024;
    shadow.shadow_bias = .004F;
    shadow.shadow_cascades = 2;
    shadow.shadow_distance = 120.0F;
    shadow.shadow_split_lambda = .7F;
    shadow.shadow_cascade_blend = .2F;
    if (!second.set_settings(shadow))
    {
        return false;
    }
    const auto count = test_support::scene_nodes(*scene).size();
    const Json identity = {{"sceneId", std::to_string(scene->id())}};
    if (!expect(
            execute("set_render_settings",
                    {{"ambientIntensity", .7},  {"exposure", 1.3},
                     {"aoEnabled", true},       {"aoRadius", 1.4},
                     {"aoStrength", .8},        {"aoPower", 2},
                     {"aoFadeStart", 40},       {"aoFadeEnd", 140},
                     {"hdrEnabled", true},      {"bloomEnabled", true},
                     {"bloomThreshold", 1.2},   {"bloomIntensity", .3},
                     {"bloomRadius", 2},        {"directionalShadows", false},
                     {"shadowCascades", 4},     {"shadowDistance", 240},
                     {"shadowSplitLambda", .5}, {"shadowCascadeBlend", .15},
                     {"iblEnabled", true},      {"iblIntensity", .5},
                     {"ssrEnabled", true},      {"ssrResolutionScale", .75},
                     {"ssrMaxSteps", 96},       {"ssrMaxDistance", 50},
                     {"ssrThickness", .2},      {"ssrRoughnessCutoff", .6}}) &&
                test_support::scene_nodes(*scene).size() == count + 1 &&
                camera_node.get_component<engine::PostProcessComponent>().valid() &&
                renderer.settings().ambient_intensity == .7F && renderer.settings().ibl_enabled &&
                renderer.settings().ibl_intensity == .5F && renderer.settings().exposure == 1.3F &&
                !second.settings().cast_shadows && renderer.settings().ao_enabled &&
                renderer.settings().ao_radius == 1.4F && renderer.settings().ao_strength == .8F &&
                renderer.settings().ao_power == 2 && renderer.settings().ao_fade_start == 40 &&
                renderer.settings().ao_fade_end == 140 && renderer.settings().hdr_enabled &&
                renderer.settings().bloom_enabled && renderer.settings().bloom_threshold == 1.2F &&
                renderer.settings().bloom_intensity == .3F &&
                renderer.settings().bloom_radius == 2 && !first.settings().cast_shadows &&
                result["shadowResolution"] == first.settings().shadow_resolution &&
                result["shadowCascades"] == first.settings().shadow_cascades &&
                result["shadowDistance"] == first.settings().shadow_distance &&
                second.settings().shadow_cascades == 4 &&
                second.settings().shadow_distance == 240.0F &&
                second.settings().shadow_split_lambda == .5F &&
                second.settings().shadow_cascade_blend == .15F &&
                commands.mutation_revision() > initial_revision,
            "scene edits create components, mark dirty and target the active shadow caster"))
    {
        std::fprintf(stderr, "scene render settings: %s\n", error.c_str());
        return false;
    }
    engine::Node environment_node = {};
    for (const auto& node : test_support::scene_nodes(*scene))
    {
        if (node.get_component<engine::EnvironmentComponent>())
        {
            environment_node = node;
            break;
        }
    }
    if (!expect(environment_node.valid(), "find the automatically created environment node"))
    {
        return false;
    }
    if (!environment_node.set_visible(false))
    {
        return false;
    }
    if (!expect(!commands.execute("undo", identity, result, error) &&
                    test_support::scene_nodes(*scene).size() == count + 1,
                "undo preserves an environment node whose local visibility was edited"))
    {
        return false;
    }
    if (!environment_node.set_visible(true))
    {
        return false;
    }
    if (!environment_node.set_enabled(false))
    {
        return false;
    }
    if (!expect(!commands.execute("undo", identity, result, error) &&
                    test_support::scene_nodes(*scene).size() == count + 1,
                "undo preserves an environment node whose local enabled state was edited"))
    {
        return false;
    }
    if (!environment_node.set_enabled(true))
    {
        return false;
    }
    auto environment_transform = environment_node.get_component<engine::Transform>();
    if (!environment_transform.set_visible(false))
    {
        return false;
    }
    if (!expect(!commands.execute("undo", identity, result, error) &&
                    test_support::scene_nodes(*scene).size() == count + 1,
                "undo preserves an environment node whose Transform visibility was edited"))
    {
        return false;
    }
    if (!environment_transform.set_visible(true))
    {
        return false;
    }
    engine::schema::SceneDataT saved;
    if (!expect(scene->build_data(saved) && saved.nodes.size() == count + 1,
                "scene snapshot contains the environment node") ||
        !expect(execute("undo", identity) && test_support::scene_nodes(*scene).size() == count &&
                    camera_node.get_component<engine::PostProcessComponent>().valid() == false &&
                    second.settings().cast_shadows && second.settings().shadow_resolution == 1024 &&
                    second.settings() == shadow && !first.settings().cast_shadows,
                "undo removes created components and restores the exact second light") ||
        !expect(
            execute("redo", identity) && test_support::scene_nodes(*scene).size() == count + 1 &&
                renderer.settings().exposure == 1.3F && !second.settings().cast_shadows &&
                renderer.settings().hdr_enabled && renderer.settings().bloom_enabled &&
                renderer.settings().bloom_threshold == 1.2F &&
                renderer.settings().bloom_intensity == .3F &&
                renderer.settings().bloom_radius == 2 && renderer.settings().ao_enabled &&
                renderer.settings().ao_radius == 1.4F && renderer.settings().ao_strength == .8F &&
                renderer.settings().ao_power == 2 && renderer.settings().ao_fade_start == 40 &&
                renderer.settings().ao_fade_end == 140 && second.settings().shadow_cascades == 4 &&
                second.settings().shadow_distance == 240.0F &&
                second.settings().shadow_split_lambda == .5F &&
                second.settings().shadow_cascade_blend == .15F,
            "redo recreates the environment with a new node identity") ||
        !expect(execute("undo", identity), "undo before checking rejected settings") ||
        !expect(!execute("set_render_settings", {{"ambientIntensity", .8}, {"exposure", 0}}) &&
                    test_support::scene_nodes(*scene).size() == count &&
                    camera_node.get_component<engine::PostProcessComponent>().valid() == false &&
                    second.settings().cast_shadows && renderer.settings().ambient_intensity == 0,
                "invalid combined edits leave no partial nodes or components"))
    {
        return false;
    }
    const auto revision = commands.mutation_revision();
    for (const auto& invalid :
         Json::array({{{"shadowCascades", 3}},
                      {{"shadowCascades", 2.0}},
                      {{"shadowDistance", 0}},
                      {{"shadowDistance", 100001}},
                      {{"shadowDistance", std::numeric_limits<double>::quiet_NaN()}},
                      {{"shadowSplitLambda", 1.1}},
                      {{"shadowCascadeBlend", .31}},
                      {{"aoEnabled", 1}},
                      {{"aoEnabled", true}, {"aoRadius", 0}},
                      {{"aoStrength", 2.1}},
                      {{"aoPower", .09}},
                      {{"aoPower", std::numeric_limits<double>::quiet_NaN()}},
                      {{"aoFadeStart", 160}, {"aoFadeEnd", 150}},
                      {{"aoFadeEnd", 100001}},
                      {{"ssrEnabled", 1}},
                      {{"ssrResolutionScale", .24}},
                      {{"ssrMaxSteps", 7}},
                      {{"ssrMaxSteps", 256.0}},
                      {{"ssrMaxDistance", 0}},
                      {{"ssrThickness", 0}},
                      {{"ssrRoughnessCutoff", 1.1}},
                      {{"hdrEnabled", 1}},
                      {{"bloomEnabled", 1}},
                      {{"bloomEnabled", true}},
                      {{"bloomThreshold", -1}},
                      {{"bloomIntensity", 10.1}},
                      {{"bloomRadius", .24}},
                      {{"bloomRadius", std::numeric_limits<double>::quiet_NaN()}}}))
    {
        if (!expect(!execute("set_render_settings", invalid) && second.settings() == shadow &&
                        commands.mutation_revision() == revision &&
                        test_support::scene_nodes(*scene).size() == count &&
                        camera_node.get_component<engine::PostProcessComponent>().valid() == false,
                    "invalid effect commands preserve components and undo history"))
        {
            return false;
        }
    }
    if (!expect(execute("redo", identity), "rejected edits preserve the redo candidate"))
    {
        return false;
    }
    const auto enabled_revision = commands.mutation_revision();
    return expect(!execute("set_render_settings", {{"hdrEnabled", false}}) &&
                      commands.mutation_revision() == enabled_revision &&
                      renderer.settings().hdr_enabled && renderer.settings().bloom_enabled &&
                      renderer.settings().bloom_threshold == 1.2F &&
                      renderer.settings().bloom_intensity == .3F &&
                      renderer.settings().bloom_radius == 2,
                  "disabling HDR cannot silently disable active Bloom") &&
           expect(
               execute("set_render_settings", {{"hdrEnabled", false}, {"bloomEnabled", false}}) &&
                   !renderer.settings().hdr_enabled && !renderer.settings().bloom_enabled &&
                   renderer.settings().ao_enabled && renderer.settings().exposure == 1.3F &&
                   renderer.settings().bloom_intensity == .3F,
               "one command can atomically disable HDR and Bloom while preserving parameters") &&
           expect(execute("undo", identity) && renderer.settings().hdr_enabled &&
                      renderer.settings().bloom_enabled,
                  "undo restores both post-process flags");
}

class TrackedResourceLoader final : public engine::ResourceLoader
{
public:
    explicit TrackedResourceLoader(engine::ResourceSystem& resources) : resources_(resources)
    {
        resources_.set_loader(this);
    }
    TrackedResourceLoader(const TrackedResourceLoader&) = delete;
    TrackedResourceLoader& operator=(const TrackedResourceLoader&) = delete;
    ~TrackedResourceLoader() noexcept override
    {
        resources_.set_loader(nullptr);
    }
    bool load(std::string_view guid, engine::ResourcePtr& output) override
    {
        if (!resources_.load(guid, output))
        {
            return false;
        }
        loaded_.emplace_back(output);
        return true;
    }
    engine::ResourcePtr find(std::string_view) const noexcept override
    {
        return {};
    }
    const std::string& last_error() const noexcept override
    {
        return resources_.last_error();
    }
    bool released() const
    {
        return std::all_of(loaded_.begin(), loaded_.end(),
                           [](const auto& resource) { return resource.expired(); });
    }

private:
    engine::ResourceSystem& resources_;
    std::vector<std::weak_ptr<const engine::Resource>> loaded_;
};

bool render_component_tests()
{
    using namespace engine;
    ResourceSystem resources;
    WorldSystem world;
    WindowSystem windows;
    RenderSystem renderer(windows, &world);
    test_support::ResourceFixture fixture;
    schema::MeshDataT geometry;
    geometry.path = "lighting/test.mesh";
    geometry.vertices = {{0, 0, 0, 0, 1, 0}, {1, 0, 0, 0, 1, 0}, {0, 0, 1, 0, 1, 0}};
    geometry.indices = {0, 2, 1};
    geometry.lightmap_texcoords = {{.1, .1}, {.9, .1}, {.1, .9}};
    schema::TextureDataT lightmap;
    lightmap.path = "lighting/test.texture";
    lightmap.width = lightmap.height = 1;
    lightmap.encoding = schema::TextureEncoding::rgba8;
    lightmap.srgb = false;
    lightmap.data = {128, 128, 128, 255};
    schema::LightProbeDataT probes;
    probes.path = "lighting/test.probes";
    probes.bounds_min = Vec3(-1, -1, -1);
    probes.bounds_max = Vec3(1, 1, 1);
    probes.grid_x = probes.grid_y = probes.grid_z = 2;
    probes.baked_transform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    probes.coefficients.resize(72, Vec3(.1, .1, .1));
    probes.valid.resize(8, 1);
    probes.source_fingerprint = "lighting-command-test";
    if (!resources.initialize({}) || !fixture.add(geometry) || !fixture.add(lightmap) ||
        !fixture.add(probes) || !fixture.publish(resources) || !world.initialize({}))
    {
        return false;
    }
    TrackedResourceLoader loader(resources);
    auto* scene = world.create_scene("Lighting commands");
    auto node = scene ? scene->create_node("Receiver") : engine::Node{};
    auto mesh = node ? node.add_component<MeshRenderer>() : MeshRenderer{};
    auto transform = node ? node.get_component<Transform>() : Transform{};
    if (!mesh || !transform)
    {
        return false;
    }
    editor::EditorCommands commands([&]() { return world.scene() ? world.scene()->id() : 0; });
    editor::RenderCommands module(renderer, world, {});
    editor::SceneCommands scene_commands(world);
    if (!module.register_commands(commands, scene_commands) ||
        !scene_commands.register_commands(commands))
    {
        return false;
    }
    Json result;
    std::string error;
    const auto execute = [&](const char* name, const Json& arguments)
    { return commands.execute(name, arguments, result, error); };
    const auto set = [&](const char* name, const Json& fields)
    {
        auto arguments = target(node);
        arguments.update(fields);
        return execute(name, arguments);
    };
    const Json identity = {{"sceneId", std::to_string(scene->id())}};
    if (!expect(!execute("get_planar_reflection", target(node)) &&
                    !set("set_planar_reflection", Json::object()) &&
                    !set("set_planar_reflection", {{"resolutionScale", .1}}) &&
                    !node.get_component<PlanarReflectionComponent>(),
                "missing queries and invalid reflection edits never create a component") ||
        !expect(set("set_planar_reflection",
                    {{"enabled", false}, {"resolutionScale", .5}, {"clipBias", .02}}) &&
                    !node.get_component<PlanarReflectionComponent>().enabled() &&
                    execute("undo", identity) && !node.get_component<PlanarReflectionComponent>() &&
                    execute("redo", identity) && execute("get_planar_reflection", target(node)) &&
                    result["resolutionScale"] == .5 && result["enabled"] == false,
                "reflection creation, enabled state and settings share undo/redo"))
    {
        return false;
    }
    if (!expect(!set("set_light_probe_volume", {{"resource", lightmap.path}}) &&
                    !node.get_component<LightProbeVolumeComponent>() &&
                    set("set_light_probe_volume", {{"resource", probes.path}}) &&
                    execute("get_light_probe_volume", target(node)) && result["valid"] == true &&
                    result["resource"] == probes.path,
                "probe command validates the resource kind before adding a component"))
    {
        return false;
    }
    auto moved = transform.settings();
    moved.position = Vec3(1, 0, 0);
    if (!expect(transform.set_settings(moved) && execute("get_light_probe_volume", target(node)) &&
                    result["valid"] == false && result["resource"] == probes.path &&
                    !result["resourceGuid"].get<std::string>().empty(),
                "probe query preserves configured identity while the baked transform is invalid"))
    {
        return false;
    }
    moved.position = Vec3(0, 0, 0);
    if (!transform.set_settings(moved))
    {
        return false;
    }
    if (!expect(set("set_light_probe_volume", {{"resource", ""}}) &&
                    !node.get_component<LightProbeVolumeComponent>().resource() &&
                    execute("undo", identity) &&
                    node.get_component<LightProbeVolumeComponent>().resource(),
                "explicitly clearing a probe binding is reversible"))
    {
        return false;
    }
    if (!expect(set("set_light_probe_volume", {{"enabled", false}}),
                "disable the configured probe"))
    {
        return false;
    }
    auto catalog = fixture.catalog;
    catalog.revision = "changed-probe-version";
    for (auto& entry : catalog.entries)
    {
        if (entry->path == probes.path)
        {
            entry->content_hash = "0000000000000000";
        }
    }
    if (!expect(resources.update_catalog(catalog, {{"content", fixture.root}}) &&
                    !execute("undo", identity) &&
                    !node.get_component<LightProbeVolumeComponent>().enabled() &&
                    fixture.publish(resources) && execute("undo", identity),
                "probe undo refuses changed resource versions and succeeds after restoring the "
                "catalog"))
    {
        return false;
    }
    if (!expect(!set("set_mesh_lighting", {{"bakeStatic", true}, {"lightmap", lightmap.path}}) &&
                    !mesh.bake_static() &&
                    mesh.data().payload.AsMeshRendererData()->lightmap_guid.empty(),
                "missing UV2 rejects the lightmap and rolls back bakeStatic"))
    {
        return false;
    }
    if (!mesh.set_mesh(geometry.path) || !mesh.material().set_float("roughness", .37F))
    {
        return false;
    }
    const auto mesh_revision = mesh.mesh_revision();
    if (!expect(set("set_mesh_lighting",
                    {{"castShadows", false}, {"bakeStatic", true}, {"lightmap", lightmap.path}}) &&
                    mesh.lightmap_resource() && execute("get_mesh_lighting", target(node)) &&
                    result["lightmap"] == lightmap.path && result["lightmapValid"] == true &&
                    result["bakeStatic"] == true && result["castShadows"] == false &&
                    execute("undo", identity) && !mesh.lightmap_resource() && mesh.cast_shadows() &&
                    !mesh.bake_static() && execute("redo", identity),
                "valid UV2 receiver supports binding, query, undo and redo"))
    {
        return false;
    }
    float roughness = 0;
    if (!expect(
            mesh.mesh_revision() == mesh_revision &&
                mesh.material().get_float("roughness", roughness) && roughness == .37F &&
                !set("set_mesh_lighting", {{"lightmap", "builtin://textures/white-srgb"}}) &&
                mesh.lightmap_resource(),
            "lighting edits preserve mesh identity, material overrides and old binding on failure"))
    {
        return false;
    }
    moved.position = Vec3(2, 0, 0);
    if (!expect(
            transform.set_settings(moved) && execute("get_mesh_lighting", target(node)) &&
                result["lightmap"] == lightmap.path && result["lightmapValid"] == false &&
                !set("set_mesh_lighting", {{"lightmap", ""}}) && !execute("undo", identity),
            "stale lightmap identity remains inspectable and cannot be silently rebound by undo"))
    {
        return false;
    }
    moved.position = Vec3(0, 0, 0);
    if (!transform.set_settings(moved) || !execute("undo", identity))
    {
        return false;
    }
    auto planar = target(node);
    planar["clipBias"] = .1;
    auto invalid = target(node);
    invalid["lightmap"] = "builtin://textures/white-srgb";
    const Json batch = {
        {"commands", Json::array({{{"command", "set_planar_reflection"}, {"arguments", planar}},
                                  {{"command", "set_mesh_lighting"}, {"arguments", invalid}}})}};
    if (!expect(!execute("batch", batch) &&
                    node.get_component<PlanarReflectionComponent>().settings().clip_bias == .02F,
                "a later mesh failure rolls back the earlier component edit in the batch"))
    {
        return false;
    }
    if (!execute("get_scene", Json::object()))
    {
        return false;
    }
    Json planar_result;
    Json probe_result;
    Json mesh_result;
    for (const auto& component : result["scene"]["nodes"][0]["components"])
    {
        if (component["type"] == "planar_reflection")
        {
            planar_result = component;
        }
        if (component["type"] == "light_probe_volume")
        {
            probe_result = component;
        }
        if (component["type"] == "mesh_renderer")
        {
            mesh_result = component;
        }
    }
    if (!expect(planar_result["enabled"] == false && planar_result["resolutionScale"] == .5 &&
                    probe_result["resource"] == probes.path && probe_result["valid"] == true &&
                    mesh_result["bakeStatic"] == false && mesh_result["lightmap"] == "" &&
                    mesh_result["lightmapValid"] == false,
                "full scene query reports component settings and configured versus valid bindings"))
    {
        return false;
    }
    if (!execute("create_node",
                 {{"sceneId", identity["sceneId"]}, {"name", "New probe"}, {"component", "empty"}}))
    {
        return false;
    }
    auto probe_target = target(test_support::scene_nodes(*scene).back());
    probe_target["resource"] = probes.path;
    if (!expect(execute("set_light_probe_volume", probe_target) && execute("undo", identity) &&
                    execute("undo", identity) && execute("redo", identity) &&
                    execute("redo", identity),
                "render component redo follows a newly created node's shared identity"))
    {
        std::fprintf(stderr, "render identity: %s\n", error.c_str());
        return false;
    }
    const auto restored =
        test_support::scene_nodes(*scene).back().get_component<LightProbeVolumeComponent>();
    if (!expect(restored && restored.resource() && restored.resource()->info().path == probes.path,
                "recreated node receives the original probe resource binding"))
    {
        return false;
    }
    // 历史仍保留探针和 Lightmap 编辑，销毁场景必须释放全部临时加载的载荷。
    world.destroy_scene();
    return expect(commands.undo_count() > 2 && loader.released(),
                  "undo history retains resource version values without retaining payloads");
}

bool render_command_tests()
{
    engine::ResourceSystem resources;
    engine::WorldSystem world;
    if (!resources.initialize({}) || !world.initialize({}))
    {
        return false;
    }
    auto* scene = world.create_scene("Render commands");
    auto node = scene ? scene->create_node("Object") : engine::Node{};
    auto camera = node ? node.add_component<engine::Camera>() : engine::Camera{};
    auto light = node ? node.add_component<engine::Light>() : engine::Light{};
    auto mesh = node ? node.add_component<engine::MeshRenderer>() : engine::MeshRenderer{};
    if (!camera || !light || !mesh || !mesh.set_material_count(2))
    {
        return false;
    }
    engine::editor::EditorCommands commands([&world]()
                                            { return world.scene() ? world.scene()->id() : 0; });
    engine::editor::SceneCommands module(world);
    if (!module.register_commands(commands))
    {
        return false;
    }
    Json result;
    std::string error;
    const auto execute = [&](std::string_view name, const Json& args)
    { return commands.execute(name, args, result, error); };
    const Json identity = {{"sceneId", std::to_string(scene->id())}};
    auto args = target(node);
    args.update({{"projection", "orthographic"}, {"orthographicSize", 3}});
    if (!expect(execute("set_camera", args) &&
                    camera.settings().projection == engine::schema::Projection::orthographic,
                "camera projection editable") ||
        !expect(execute("undo", identity) &&
                    camera.settings().projection == engine::schema::Projection::perspective,
                "camera undo restores projection"))
    {
        return false;
    }
    args = target(node);
    args.update({{"kind", "spot"},
                 {"range", 7},
                 {"innerAngle", 15},
                 {"outerAngle", 25},
                 {"castShadows", true},
                 {"shadowResolution", 512},
                 {"shadowBias", .005}});
    if (!expect(execute("set_light", args) && light.settings().range == 7 &&
                    light.settings().kind == engine::schema::LightKind::spot &&
                    light.settings().cast_shadows && light.settings().shadow_resolution == 512 &&
                    light.settings().shadow_bias == .005F,
                "spotlight editing") ||
        !expect(execute("undo", identity) &&
                    light.settings().kind == engine::schema::LightKind::directional,
                "spotlight undo"))
    {
        return false;
    }
    for (const Json& invalid :
         {Json{{"castShadows", 1}}, Json{{"shadowResolution", 255}}, Json{{"shadowBias", .2}}})
    {
        args = target(node);
        args.update(invalid);
        const auto before = light.settings();
        if (!expect(!execute("set_light", args) && light.settings() == before,
                    "invalid local shadow values preserve the whole light"))
        {
            return false;
        }
    }
    args = target(node);
    args.update({{"slot", 1}, {"mode", "alpha_blend"}});
    if (!expect(execute("set_material_blend_mode", args) &&
                    mesh.material(1).blend_mode() == engine::schema::BlendMode::alpha_blend &&
                    mesh.material().blend_mode() == engine::schema::BlendMode::opaque &&
                    execute("undo", identity) &&
                    mesh.material(1).blend_mode() == engine::schema::BlendMode::opaque &&
                    execute("redo", identity),
                "material blend mode selects a slot and supports undo/redo"))
    {
        return false;
    }
    args = target(node);
    args.update(
        {{"slot", 1}, {"name", "baseColorTexture"}, {"path", "builtin://textures/white-linear"}});
    const auto original = mesh.material(1).texture("baseColorTexture");
    const auto untouched = mesh.material().texture("baseColorTexture");
    if (!expect(execute("set_material_texture", args) &&
                    mesh.material(1).texture("baseColorTexture") &&
                    mesh.material(1).texture("baseColorTexture")->info().path ==
                        "builtin://textures/white-linear" &&
                    !same_texture(mesh.material(1).texture("baseColorTexture"), original) &&
                    mesh.material().texture("baseColorTexture") == untouched,
                "texture slot selection") ||
        !expect(execute("undo", identity) &&
                    same_texture(mesh.material(1).texture("baseColorTexture"), original) &&
                    mesh.material().texture("baseColorTexture") == untouched,
                "texture undo restores resource identity and data") ||
        !expect(execute("redo", identity), "texture redo"))
    {
        return false;
    }
    args["path"] = "missing.png";
    const auto previous = mesh.material(1).texture("baseColorTexture");
    if (!expect(!execute("set_material_texture", args) &&
                    mesh.material(1).texture("baseColorTexture") == previous,
                "failed texture assignment is atomic"))
    {
        return false;
    }
    args = target(node);
    args.update({{"slot", 1}, {"name", "roughness"}, {"type", "float"}, {"value", 0.8}});
    float value = 0;
    if (!expect(execute("set_material_parameter", args) &&
                    mesh.material(1).get_float("roughness", value) && value == 0.8F,
                "numeric material slot editing"))
    {
        return false;
    }
    auto bad_camera = target(node);
    bad_camera["nearClip"] = -1;
    Json batch = {
        {"commands", Json::array({{{"command", "set_material_texture"},
                                   {"arguments",
                                    [&]()
                                    {
                                        auto t = target(node);
                                        t.update({{"slot", 1},
                                                  {"name", "baseColorTexture"},
                                                  {"path", "builtin://textures/white-srgb"}});
                                        return t;
                                    }()}},
                                  {{"command", "set_camera"}, {"arguments", bad_camera}}})}};
    if (!expect(!execute("batch", batch) &&
                    same_texture(mesh.material(1).texture("baseColorTexture"), previous) &&
                    mesh.material().texture("baseColorTexture") == untouched,
                "batch rolls texture back when camera fails"))
    {
        return false;
    }
    return expect(execute("get_scene", Json::object()) &&
                      result["scene"]["nodes"][0]["components"][3]["materials"].size() == 2 &&
                      result["scene"]["nodes"][0]["components"][3]["materials"][1]["blendMode"] ==
                          "alpha_blend",
                  "snapshot exposes all material slots");
}

bool state_command_tests()
{
    using namespace engine;
    WorldSystem world;
    if (!world.initialize({}))
    {
        return false;
    }
    auto* scene = world.create_scene("State commands");
    auto parent = scene ? scene->create_node("Parent") : engine::Node{};
    auto child = scene ? scene->create_node("Child") : engine::Node{};
    auto light = child ? child.add_component<Light>() : Light{};
    if (!parent || !light || !child.set_parent(parent))
    {
        return false;
    }
    editor::SceneCommands module(world);
    editor::EditorCommands commands([&] { return world.scene() ? world.scene()->id() : 0; });
    if (!module.register_commands(commands))
    {
        return false;
    }
    Json result;
    std::string error;
    const auto execute = [&](const char* name, const Json& arguments)
    { return commands.execute(name, arguments, result, error); };
    const auto set = [&](const char* name, const Node& node, const Json& changes)
    {
        auto arguments = target(node);
        arguments.update(changes);
        return execute(name, arguments);
    };
    const Json identity = {{"sceneId", std::to_string(scene->id())}};
    const auto instance = light.instance_id();
    auto light_settings = light.settings();
    light_settings.intensity = 2.5F;
    if (!light.set_settings(light_settings) ||
        !expect(set("set_component_state", child,
                    {{"componentType", "LightData"}, {"visible", false}}) &&
                    set("set_node_state", parent, {{"enabled", false}, {"visible", false}}) &&
                    child.enabled() && child.visible() && !child.active() &&
                    !child.visible_in_hierarchy() && light.enabled() && !light.active() &&
                    !light.visible_in_hierarchy(),
                "parent flags affect descendants without changing their local flags") ||
        !expect(execute("get_scene", Json::object()) &&
                    result["scene"]["nodes"][1]["enabled"] == true &&
                    result["scene"]["nodes"][1]["active"] == false &&
                    result["scene"]["nodes"][1]["visible"] == true &&
                    result["scene"]["nodes"][1]["visibleInHierarchy"] == false &&
                    result["scene"]["nodes"][1]["components"][1]["componentType"] == "LightData" &&
                    result["scene"]["nodes"][1]["components"][1]["visible"] == false,
                "scene query distinguishes local and inherited state") ||
        !expect(execute("undo", identity) && child.active() && child.visible_in_hierarchy() &&
                    light.active() && !light.visible_in_hierarchy() && execute("undo", identity) &&
                    light.visible_in_hierarchy() && light.instance_id() == instance &&
                    light.settings() == light_settings,
                "undo restores flags without recreating a component or its payload") ||
        !expect(execute("redo", identity) && execute("redo", identity) && !parent.enabled() &&
                    !parent.visible(),
                "state edits redo through the normal history"))
    {
        return false;
    }
    if (!child.set_enabled(false))
    {
        return false;
    }
    if (!child.set_visible(false))
    {
        return false;
    }
    if (!expect(execute("undo", identity) && !child.enabled() && !child.visible() &&
                    !child.active() && !child.visible_in_hierarchy(),
                "restoring a parent preserves a child's independent disabled and hidden flags") ||
        !expect(!set("set_node_state", parent, Json::object()) &&
                    !set("set_node_state", parent, {{"enabled", 1}}) &&
                    !set("set_component_state", child,
                         {{"componentType", "CameraData"}, {"enabled", false}}),
                "state commands reject missing flags, wrong types and absent components"))
    {
        return false;
    }
    commands.clear_history();
    if (!set("set_node_state", parent, {{"enabled", false}}))
    {
        return false;
    }
    if (!parent.set_enabled(true))
    {
        return false;
    }
    if (!expect(!execute("undo", identity) && parent.enabled(),
                "stale local state refuses undo without changing the object"))
    {
        return false;
    }
    commands.clear_history();
    if (!expect(set("set_audio_listener", child, {{"volume", .8}}) &&
                    set("set_component_state", child,
                        {{"componentType", "AudioListenerData"},
                         {"enabled", false},
                         {"visible", false}}) &&
                    set("set_audio_listener", child, {{"volume", .4}}),
                "generic payload edit accepts a disabled and hidden component"))
    {
        return false;
    }
    const auto listener = child.get_component(ComponentType::AudioListenerData);
    if (!expect(listener && !listener.enabled() && !listener.visible() &&
                    execute("undo", identity) && !listener.enabled() && !listener.visible() &&
                    execute("redo", identity) && !listener.enabled() && !listener.visible(),
                "payload edits and their undo/redo preserve both local state flags"))
    {
        return false;
    }
    commands.clear_history();
    if (!execute("create_node",
                 {{"sceneId", identity["sceneId"]}, {"name", "Recreated"}, {"component", "light"}}))
    {
        return false;
    }
    const auto created = test_support::scene_nodes(*scene).back();
    const auto original_id = created.id();
    if (!set("set_component_state", created,
             {{"componentType", "LightData"}, {"enabled", false}, {"visible", false}}) ||
        !execute("undo", identity) || !execute("undo", identity) || !execute("redo", identity) ||
        !execute("redo", identity))
    {
        std::fprintf(stderr, "state edit recreation: %s\n", error.c_str());
        return false;
    }
    const auto recreated = test_support::scene_nodes(*scene).back();
    const auto recreated_light = recreated.get_component<Light>();
    return expect(recreated.id() != original_id && recreated_light && !recreated_light.enabled() &&
                      !recreated_light.visible() &&
                      !execute("set_node_state", {{"sceneId", identity["sceneId"]},
                                                  {"nodeId", std::to_string(original_id)},
                                                  {"enabled", false}}),
                  "internal redo follows shared node identity while external stale IDs fail");
}

bool combined_component_history_tests()
{
    using namespace engine;
    ResourceSystem resources;
    WorldSystem world;
    if (!resources.initialize({}) || !world.initialize({}))
    {
        return false;
    }
    auto* scene = world.create_scene("Combined component history");
    auto mesh_node = scene ? scene->create_node("Mesh") : Node{};
    auto light_node = scene ? scene->create_node("Light") : Node{};
    auto mesh = mesh_node ? mesh_node.add_component<MeshRenderer>() : MeshRenderer{};
    auto light = light_node ? light_node.add_component<Light>() : Light{};
    if (!mesh || !light || !mesh.set_material_count(2))
    {
        return false;
    }
    float before_roughness = 0;
    if (!mesh.material(1).get_float("roughness", before_roughness))
    {
        return false;
    }
    const auto before_light = light.settings();
    editor::SceneCommands module(world);
    editor::EditorCommands commands([&] { return world.scene() ? world.scene()->id() : 0; });
    if (!module.register_commands(commands))
    {
        return false;
    }
    Json result;
    std::string error;
    const auto execute = [&](const char* name, const Json& arguments)
    { return commands.execute(name, arguments, result, error); };
    const Json identity = {{"sceneId", std::to_string(scene->id())}};
    if (!execute("create_node", {{"sceneId", identity["sceneId"]},
                                 {"name", "Recreated camera"},
                                 {"component", "camera"}}))
    {
        return false;
    }
    const auto original = test_support::scene_nodes(*scene).back();
    const auto original_id = original.id();
    auto camera = original.get_component<Camera>();
    const auto before_camera = camera.settings();
    auto transform_args = target(original);
    transform_args["position"] = {2, 1, 3};
    auto camera_args = target(original);
    camera_args.update({{"fovDegrees", 73}, {"frustumCullingEnabled", true}});
    auto material_args = target(mesh_node);
    material_args.update({{"slot", 1}, {"name", "roughness"}, {"type", "float"}, {"value", .75}});
    auto light_args = target(light_node);
    light_args["intensity"] = 6.5;
    const Json batch = {
        {"commands",
         Json::array({{{"command", "set_transform"}, {"arguments", transform_args}},
                      {{"command", "set_camera"}, {"arguments", camera_args}},
                      {{"command", "set_material_parameter"}, {"arguments", material_args}},
                      {{"command", "set_light"}, {"arguments", light_args}}})}};
    const auto applied = [&](Node node)
    {
        float roughness = 0;
        const auto current = node.get_component<Camera>();
        return current && node.get_component<Transform>().settings().position.x() == 2 &&
               current.settings().fov_degrees == 73 && current.settings().frustum_culling_enabled &&
               light.settings().intensity == 6.5F &&
               mesh.material(1).get_float("roughness", roughness) && roughness == .75F;
    };
    if (!expect(execute("batch", batch) && applied(original) && commands.undo_count() == 2,
                "four component types share one transaction after node creation"))
    {
        return false;
    }
    if (!expect(mesh.material(1).set_float("roughness", .9F) && !execute("undo", identity) &&
                    light.settings().intensity == 6.5F && camera.settings().fov_degrees == 73 &&
                    original.get_component<Transform>().settings().position.x() == 2 &&
                    commands.undo_count() == 2 && commands.redo_count() == 0,
                "a stale material rejects mixed undo and restores the light already reverted"))
    {
        return false;
    }
    auto unrelated_camera = camera.settings();
    unrelated_camera.near_clip = .25F;
    float roughness = 0;
    if (!expect(mesh.material(1).set_float("roughness", .75F) &&
                    camera.set_settings(unrelated_camera) && execute("undo", identity) &&
                    camera.settings().fov_degrees == before_camera.fov_degrees &&
                    camera.settings().near_clip == .25F &&
                    camera.settings().frustum_culling_enabled ==
                        before_camera.frustum_culling_enabled &&
                    original.get_component<Transform>().settings().position.x() == 0 &&
                    light.settings() == before_light &&
                    mesh.material(1).get_float("roughness", roughness) &&
                    roughness == before_roughness,
                "mixed undo restores edited fields and preserves an unrelated camera field"))
    {
        return false;
    }
    if (!expect(camera.set_settings(before_camera) && execute("undo", identity) &&
                    !scene->find_node(original_id) && execute("redo", identity),
                "node creation remains undoable after the mixed transaction is reverted"))
    {
        return false;
    }
    const auto recreated = test_support::scene_nodes(*scene).back();
    if (!expect(recreated.id() != original_id && execute("redo", identity) && applied(recreated) &&
                    !execute("set_camera", camera_args) && commands.undo_count() == 2,
                "mixed redo follows shared identities while old external node IDs stay invalid"))
    {
        std::fprintf(stderr, "combined component history: %s\n", error.c_str());
        return false;
    }
    if (!execute("get_scene", Json::object()))
    {
        return false;
    }
    Json camera_snapshot;
    Json mesh_snapshot;
    Json light_snapshot;
    for (const auto& node : result["scene"]["nodes"])
    {
        for (const auto& component : node["components"])
        {
            if (component["type"] == "camera")
            {
                camera_snapshot = component;
            }
            else if (component["type"] == "mesh_renderer")
            {
                mesh_snapshot = component;
            }
            else if (component["type"] == "light")
            {
                light_snapshot = component;
            }
        }
    }
    if (!expect(camera_snapshot.is_object() && light_snapshot.is_object() &&
                    mesh_snapshot.is_object() && mesh_snapshot["materials"].is_array() &&
                    mesh_snapshot["materials"].size() == 2 &&
                    mesh_snapshot["materials"][1]["parameters"].is_array(),
                "scene query includes all edited component and material parameter shapes"))
    {
        return false;
    }
    const auto& slot = mesh_snapshot["materials"][1];
    const auto parameter =
        std::find_if(slot["parameters"].begin(), slot["parameters"].end(),
                     [](const auto& value) { return value["name"] == "roughness"; });
    return expect(result["totalNodes"] == 3 && result["offset"] == 0 &&
                      result["scene"]["activeCameraNodeId"].is_null() &&
                      camera_snapshot["fovDegrees"] == 73 &&
                      camera_snapshot["frustumCullingEnabled"] == true &&
                      light_snapshot["intensity"] == 6.5 &&
                      mesh_snapshot["materials"].size() == 2 &&
                      mesh_snapshot["material"] == mesh_snapshot["materials"][0] &&
                      slot["blendMode"] == "opaque" && slot["textures"].is_array() &&
                      parameter != slot["parameters"].end() && (*parameter)["type"] == "float" &&
                      (*parameter)["value"] == .75,
                  "scene query preserves component and per-slot material snapshot JSON");
}

bool command_tests()
{
    engine::ResourceSystem resources;
    engine::WorldSystem world;
    const engine::EngineConfig config;
    if (!expect(resources.initialize(config) && world.initialize(config), "initialize systems"))
    {
        return false;
    }
    engine::editor::SceneCommands module(world);
    engine::editor::EditorCommands commands(
        [&world]() { return world.scene() == nullptr ? 0 : world.scene()->id(); });
    Json result;
    std::string error;
    const auto execute = [&](std::string_view name, const Json& arguments)
    { return commands.execute(name, arguments, result, error); };
    if (!expect(module.register_commands(commands), "register independent scene module") ||
        !expect(!module.register_commands(commands), "duplicate names are not overwritten") ||
        !expect(execute("get_scene", Json::object()) && result["scene"].is_null(),
                "idle scene query is null") ||
        !expect(!execute("unknown", Json::object()) && commands.undo_count() == 0,
                "unknown commands do not add history"))
    {
        return false;
    }
    auto* scene = world.create_scene("Commands");
    auto cube = scene != nullptr ? scene->create_node("Cube") : engine::Node{};
    auto lamp = scene != nullptr ? scene->create_node("Light") : engine::Node{};
    auto mesh = cube.valid() ? cube.add_component<engine::MeshRenderer>() : engine::MeshRenderer{};
    auto light = lamp.valid() ? lamp.add_component<engine::Light>() : engine::Light{};
    if (!expect(cube.valid() && lamp.valid() && mesh.valid() && light.valid(),
                "create runtime nodes and components"))
    {
        return false;
    }
    auto transform = cube.get_component<engine::Transform>();
    const Json identity = {{"sceneId", std::to_string(scene->id())}};
    auto position = target(cube);
    position["position"] = {1, 2, 3};
    if (!expect(execute("get_scene", Json::object()) &&
                    result["scene"]["nodes"][0]["nodeId"] == std::to_string(cube.id()) &&
                    result["scene"]["nodes"][0]["components"].size() == 2,
                "query exposes complete string identities and component data") ||
        !expect(execute("set_transform", position) && transform.settings().position.x() == 1 &&
                    commands.undo_count() == 1,
                "apply transform once") ||
        !expect(execute("undo", identity) && transform.settings().position.x() == 0 &&
                    cube.get_component<engine::Transform>() == transform &&
                    scene->find_node(cube.id()) == cube,
                "undo preserves node and component identities") ||
        !expect(execute("redo", identity) && transform.settings().position.x() == 1,
                "redo reapplies the same edit"))
    {
        return false;
    }
    auto changed = transform.settings();
    changed.rotation.mutate_y(50);
    if (!expect(transform.set_settings(changed) && execute("undo", identity) &&
                    transform.settings().rotation.y() == 50,
                "undo preserves unrelated script changes") ||
        !expect(execute("redo", identity), "redo after unrelated field change"))
    {
        return false;
    }
    changed = transform.settings();
    changed.position.mutate_x(99);
    if (!expect(transform.set_settings(changed) && !execute("undo", identity) &&
                    transform.settings().position.x() == 99 && commands.undo_count() == 1,
                "stale undo cannot overwrite a script change"))
    {
        return false;
    }
    commands.clear_history();
    auto invalid = position;
    invalid["scale"] = {1, 0, 1};
    if (!expect(!execute("set_transform", invalid) && transform.settings().position.x() == 99 &&
                    commands.undo_count() == 0,
                "invalid setter leaves all fields unchanged"))
    {
        return false;
    }
    invalid = position;
    invalid["nodeId"] = cube.id();
    if (!expect(!execute("set_transform", invalid), "numeric JSON node IDs are rejected"))
    {
        return false;
    }
    invalid["nodeId"] = "18446744073709551616";
    if (!expect(!execute("set_transform", invalid), "uint64 overflow is rejected"))
    {
        return false;
    }
    auto bad_light = target(cube);
    bad_light["intensity"] = 2;
    const Json failed_batch = {{"commands",
                                {{{"command", "set_transform"}, {"arguments", position}},
                                 {{"command", "set_light"}, {"arguments", bad_light}}}}};
    if (!expect(!execute("batch", failed_batch) && transform.settings().position.x() == 99 &&
                    commands.undo_count() == 0,
                "failed batch rolls back earlier edits without creating history"))
    {
        return false;
    }
    auto lighting = target(lamp);
    lighting["intensity"] = 9;
    const Json batch = {{"commands",
                         {{{"command", "set_transform"}, {"arguments", position}},
                          {{"command", "set_light"}, {"arguments", lighting}}}}};
    const float old_light = light.settings().intensity;
    if (!expect(execute("batch", batch) && commands.undo_count() == 1 &&
                    light.settings().intensity == 9,
                "successful batch is one history item"))
    {
        return false;
    }
    changed = transform.settings();
    changed.position.mutate_x(77);
    if (!expect(transform.set_settings(changed) && !execute("undo", identity) &&
                    transform.settings().position.x() == 77 && light.settings().intensity == 9,
                "stale batch undo restores any members it already reverted"))
    {
        return false;
    }
    changed.position = {1, 2, 3};
    if (!expect(transform.set_settings(changed) && execute("undo", identity) &&
                    transform.settings().position.x() == 99 &&
                    light.settings().intensity == old_light && commands.redo_count() == 1,
                "one undo restores the whole batch"))
    {
        return false;
    }
    auto external_light = light.settings();
    external_light.intensity = 14;
    if (!expect(light.set_settings(external_light) && !execute("redo", identity) &&
                    transform.settings().position.x() == 99 && light.settings().intensity == 14,
                "stale batch redo rolls back its earlier applied members"))
    {
        return false;
    }
    external_light.intensity = old_light;
    if (!expect(light.set_settings(external_light) && execute("redo", identity) &&
                    transform.settings().position.x() == 1 && light.settings().intensity == 9,
                "redo restores whole batch after target values match"))
    {
        return false;
    }
    commands.clear_history();
    auto second_position = position;
    second_position["position"] = {4, 5, 6};
    const Json repeated = {{"commands",
                            {{{"command", "set_transform"}, {"arguments", position}},
                             {{"command", "set_transform"}, {"arguments", second_position}}}}};
    if (!expect(execute("batch", repeated) && execute("undo", identity) &&
                    transform.settings().position.x() == 1 && execute("redo", identity) &&
                    transform.settings().position.x() == 4,
                "batch records sequential changes to the same property"))
    {
        return false;
    }
    auto parameter = target(cube);
    parameter.update({{"name", "roughness"}, {"type", "float"}, {"value", 0.7}});
    float roughness = 0;
    if (!expect(execute("set_material_parameter", parameter) &&
                    mesh.material().get_float("roughness", roughness) &&
                    std::abs(roughness - 0.7F) < 0.0001F,
                "material command uses the existing native binding") ||
        !expect(execute("undo", identity) && mesh.material().get_float("roughness", roughness) &&
                    std::abs(roughness - 0.4F) < 0.0001F,
                "material undo restores only the named parameter"))
    {
        return false;
    }
    parameter["value"] = 2;
    if (!expect(!execute("set_material_parameter", parameter) &&
                    mesh.material().get_float("roughness", roughness) && roughness == 0.4F,
                "native material bounds reject invalid values"))
    {
        return false;
    }
    commands.clear_history();
    for (int index = 0; index < 130; ++index)
    {
        position["position"] = {index, 0, 0};
        if (!execute("set_transform", position))
        {
            return false;
        }
    }
    if (!expect(commands.undo_count() == 128, "history is bounded") ||
        !expect(
            !execute("batch",
                     {{"commands", Json::array({{{"command", "undo"}, {"arguments", identity}}})}}),
            "batch cannot contain history operations"))
    {
        return false;
    }
    world.destroy_scene();
    if (!expect(execute("get_scene", Json::object()) && result["scene"].is_null() &&
                    commands.undo_count() == 0 && !execute("set_transform", position),
                "stopping invalidates old scene edits and history"))
    {
        return false;
    }
    auto* replacement = world.create_scene("Replacement");
    return expect(replacement != nullptr && !execute("undo", identity) &&
                      !execute("set_transform", position),
                  "new scene cannot reuse old generation edits");
}

class CountEdit final : public engine::editor::EditorEdit
{
public:
    explicit CountEdit(int& value) : value_(value), before_(value)
    {
    }
    bool apply(std::string& error) override
    {
        if (value_ != before_)
        {
            error = "stale counter";
            return false;
        }
        ++value_;
        return true;
    }
    bool revert(std::string& error) override
    {
        if (value_ != before_ + 1)
        {
            error = "stale counter";
            return false;
        }
        --value_;
        return true;
    }

private:
    int& value_;
    int before_;
};

bool write(const fs::path& path, const Json& value)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << value.dump();
    output.close();
    return static_cast<bool>(output);
}

Json read(const fs::path& path)
{
    std::string text;
    return engine::ResourceSystem::read_text(path, text) ? Json::parse(text, nullptr, false)
                                                         : Json();
}

bool channel_tests(const fs::path& root)
{
    int count = 0;
    engine::editor::EditorCommands commands([]() { return 1; });
    if (!commands.register_command("count", true,
                                   [&count](const Json&, Json& result,
                                            std::unique_ptr<engine::editor::EditorEdit>& edit,
                                            std::string&)
                                   {
                                       result = {{"value", count + 1}};
                                       edit = std::make_unique<CountEdit>(count);
                                       return true;
                                   }) ||
        !commands.register_command("large", false,
                                   [](const Json&, Json& result,
                                      std::unique_ptr<engine::editor::EditorEdit>&, std::string&)
                                   {
                                       result = {{"content", std::string(1024 * 1024, 'x')}};
                                       return true;
                                   }))
    {
        return false;
    }
    const std::string identity = engine::PathUtils::to_utf8(root);
    engine::editor::EditorCommandChannel channel(commands, root, identity, "session_a");
    const auto request = [&](std::string_view id, std::string_view command = "count",
                             std::string_view session = "session_a")
    {
        return Json{{"session", session},
                    {"projectRoot", identity},
                    {"requestId", id},
                    {"command", command},
                    {"arguments", Json::object()}};
    };
    const auto input = root / "editor-command.json";
    const auto latest = root / "editor-receipt.json";
    const auto originals = root / "editor-receipts/session_a";
    if (!expect(write(input, request("first")) && channel.poll() && count == 1 &&
                    read(originals / "first.json")["success"] == true &&
                    channel.last_receipt() == read(originals / "first.json"),
                "channel exposes only durably published receipts") ||
        !expect(channel.poll() && count == 1, "unchanged requests do not execute again") ||
        !expect(write(input, request("second")) && channel.poll() && count == 2,
                "new request executes") ||
        !expect(write(input, request("first")) && channel.poll() && count == 2 &&
                    read(latest)["result"]["value"] == 1,
                "historical request replays its original receipt") ||
        !expect(write(input, request("foreign", "count", "old_session")) && channel.poll() &&
                    count == 2,
                "foreign session cannot execute"))
    {
        return false;
    }
    std::error_code code;
    fs::remove(latest, code);
    if (!code)
    {
        fs::create_directory(latest, code);
    }
    if (code ||
        !expect(write(input, request("third")) && !channel.poll() && count == 3 &&
                    channel.last_receipt()["requestId"] == "first",
                "receipt write failure retains the completed effect without publishing status") ||
        !expect(write(input, request("fourth")) && !channel.poll() && count == 3,
                "pending receipt blocks subsequent command execution"))
    {
        return false;
    }
    fs::remove(latest, code);
    if (code ||
        !expect(channel.poll() && count == 3 &&
                    read(originals / "third.json")["result"]["value"] == 3 &&
                    channel.last_receipt()["requestId"] == "third" && channel.poll() && count == 4,
                "retry exposes the recovered receipt before the next request") ||
        !expect(write(input, request("huge", "large")) && channel.poll() &&
                    read(latest)["success"] == false && read(latest)["result"].is_null() &&
                    channel.poll(),
                "oversized query returns a bounded failure receipt"))
    {
        return false;
    }
    fs::remove(originals / "first.json", code);
    if (code || !expect(write(input, request("first")) && !channel.poll() && count == 4,
                        "deleted receipt cannot cause duplicate execution"))
    {
        return false;
    }
    auto invalid = request("../escape");
    return expect(write(input, invalid) && !channel.poll() && count == 4,
                  "request identifiers cannot escape receipt directory");
}
} // namespace

int main()
{
    if (!state_command_tests() || !combined_component_history_tests() || !command_tests() ||
        !render_command_tests() || !scene_render_settings_tests() || !render_component_tests())
    {
        return 1;
    }
    std::error_code code;
    const auto temporary = fs::temp_directory_path(code);
    if (code)
    {
        return 1;
    }
    const auto root = fs::weakly_canonical(temporary, code) /
                      ("engine-editor-commands-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (code)
    {
        return 1;
    }
    fs::create_directories(root, code);
    if (code)
    {
        return 1;
    }
    const bool passed = channel_tests(root);
    fs::remove_all(root, code);
    return passed && !code ? 0 : 1;
}
