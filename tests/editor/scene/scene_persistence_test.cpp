#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/scene_commands.hpp"
#include "editor/project/project_system.hpp"
#include "editor/render/render_commands.hpp"
#include "editor/scene/scene_persistence.hpp"
#include "file_text.hpp"
#include "plugins/editor/modeling/modeling_commands.hpp"
#include "plugins/editor/modeling/modeling_mesh_importer.hpp"
#include "resource_fixture.hpp"
#include "resource_services.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/resource/types/shader_resource.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/light_probe_volume_component.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/planar_reflection_component.hpp"
#include "runtime/scene/post_process_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"
#include "scene_snapshot.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace engine;

bool expect(bool condition, const char* message, const std::string& error = {})
{
    if (!condition)
    {
        std::fprintf(stderr, "scene persistence: %s: %s\n", message, error.c_str());
    }
    return condition;
}

Node named(Scene& scene, std::string_view name)
{
    for (const auto& node : test_support::scene_nodes(scene))
    {
        if (node.name() == name)
        {
            return node;
        }
    }
    return {};
}

struct SourceSnapshot
{
    std::string content;
    fs::file_time_type modified;
};

using SourceSnapshots = std::map<fs::path, SourceSnapshot>;

bool snapshot_sources(const fs::path& root, SourceSnapshots& output)
{
    output.clear();
    std::error_code ec;
    fs::recursive_directory_iterator current(root, ec);
    const fs::recursive_directory_iterator end;
    while (!ec && current != end)
    {
        if (current->is_regular_file(ec))
        {
            SourceSnapshot snapshot;
            snapshot.modified = current->last_write_time(ec);
            if (ec || !test_support::read_text(current->path(), snapshot.content))
            {
                return false;
            }
            output.emplace(current->path(), std::move(snapshot));
        }
        if (ec)
        {
            return false;
        }
        current.increment(ec);
    }
    return !ec;
}

bool unchanged_sources(const SourceSnapshots& before, const SourceSnapshots& after,
                       const fs::path& changed = {})
{
    if (before.size() != after.size())
    {
        return false;
    }
    for (const auto& [path, snapshot] : before)
    {
        const auto found = after.find(path);
        if (found == after.end() ||
            (path != changed && (found->second.content != snapshot.content ||
                                 found->second.modified != snapshot.modified)))
        {
            return false;
        }
    }
    return true;
}

const schema::MeshRendererDataT* saved_mesh(const schema::SceneDataT& scene, std::uint64_t node_id)
{
    for (const auto& node : scene.nodes)
    {
        if (!node || !node->info || node->info->properties->id != node_id)
        {
            continue;
        }
        for (const auto& component : node->components)
        {
            if (component && component->payload.AsMeshRendererData())
            {
                return component->payload.AsMeshRendererData();
            }
        }
    }
    return nullptr;
}

bool write_text(const fs::path& path, std::string_view text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    return static_cast<bool>(output);
}

bool compound_scene_round_trip(const fs::path& root)
{
    schema::SceneDataT source;
    source.schema_version = 3;
    source.info = std::make_unique<schema::SceneInfoT>();
    source.info->name = "Compound collider";
    auto node = std::make_unique<schema::NodeDataT>();
    node->info = test_support::node_info();
    node->info->properties->id = 1;
    node->info->properties->name = "One compound";
    node->info->properties->enabled = false;
    node->info->properties->visible = false;
    auto transform = test_support::component_data();
    transform->payload.Set(schema::TransformDataT{});
    node->components.push_back(std::move(transform));
    auto collider = test_support::component_data();
    schema::ColliderDataT data;
    data.shape = schema::ColliderShape::compound;
    for (const double offset : {-1.0, 1.0})
    {
        auto hull = std::make_unique<schema::ColliderHullDataT>();
        hull->vertices = {{offset, 0, 0}, {offset + 0.5, 0, 0}, {offset, 0.5, 0}, {offset, 0, 0.5}};
        data.hulls.push_back(std::move(hull));
    }
    collider->payload.Set(std::move(data));
    collider->flags->enabled = false;
    collider->flags->visible = false;
    node->components.push_back(std::move(collider));
    source.nodes.push_back(std::move(node));
    schema::SceneDataT restored;
    std::string error;
    const auto path = root / "cache/compound.scene";
    return expect(engine::editor::SceneImporter::write(path, source, error) &&
                      engine::editor::SceneImporter::read(path, restored, error) &&
                      source == restored,
                  "compound hull vertices round trip through the standard scene TOML codec", error);
}

bool retained_shader_save(const fs::path& root)
{
    test_support::ResourceFixture files;
    test_support::ResourceServices services;
    WorldSystem world(&services.loader);
    if (!services.initialize({}) || !services.prepare_builtins() || !world.initialize({}))
    {
        return false;
    }
    ResourceReference builtin;
    if (!services.load("builtin://shaders/pbr", builtin))
    {
        return false;
    }
    auto shader = *builtin.data<ShaderResource>();
    shader.path = "shaders/held.shader";
    if (!files.add(shader) || !files.publish(services.core) || !services.prepare_builtins() ||
        !services.load(shader.path))
    {
        return false;
    }
    auto* scene = world.create_scene("Held shader");
    auto node = scene ? scene->create_node("Mesh") : Node{};
    auto mesh = node.add_component<MeshRenderer>();
    if (!mesh || !mesh.set_mesh("builtin://meshes/cube") ||
        !mesh.material().set_shader(shader.path) || !mesh.material().set_float("roughness", 0.37F))
    {
        return false;
    }
    const auto* held = test_support::get_resource(services.loader, mesh.material().shader());
    const auto guid = held ? held->info().guid : std::string{};
    files.catalog.entries.clear();
    ResourceHandle unavailable;
    if (!expect(!guid.empty() && files.publish(services.core) &&
                    !services.loader.load(shader.path, unavailable),
                "current catalog no longer resolves the retained shader path"))
    {
        return false;
    }
    std::vector<engine::editor::SceneSavedResource> saved;
    engine::editor::ScenePersistence persistence(
        world, root, {},
        [&](std::string_view, std::span<const engine::editor::SceneSavedResource> items)
        { saved.assign(items.begin(), items.end()); });
    engine::editor::EditorCommands commands([&]() { return scene->id(); });
    Json result;
    std::string error;
    if (!expect(persistence.register_commands(commands) &&
                    commands.execute("save_scene",
                                     {{"sceneId", std::to_string(scene->id())},
                                      {"path", "scenes/held-shader.scene"}},
                                     result, error),
                "save uses retained shader identity after catalog removal", error))
    {
        return false;
    }
    for (const auto& item : saved)
    {
        if (item.source.extension() == ".material")
        {
            std::string text;
            return expect(test_support::read_text(item.source, text) &&
                              text.find(guid) != std::string::npos,
                          "exported material references the held shader GUID");
        }
    }
    return expect(false, "modified material was exported");
}

bool run(const fs::path& root)
{
    // 与 editor_app 同样装配项目系统，捕获跨模块 TOML ABI/符号冲突。
    engine::editor::ProjectSystem project(PathUtils::to_utf8(root));
    engine::ThreadSystem threads;
    test_support::ResourceServices services(&threads);
    auto& resources = services.core;
    WorldSystem world(&services.loader);
    engine::TaskSystem tasks(threads, {"Editor", 4, engine::TaskPriority::low});
    engine::editor::AssetPipelineSystem pipeline(tasks);
    if (!project.initialize({}) ||
        !((threads.initialize({}) && services.initialize({})) && services.prepare_builtins()) ||
        !world.initialize({}) || !tasks.initialize({}) || !pipeline.initialize({}) ||
        !compound_scene_round_trip(root))
    {
        return false;
    }
    std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> importer =
        std::make_unique<engine::plugins::modeling::ModelingMeshImporter>();
    if (!pipeline.set_importer(importer))
    {
        return false;
    }
    auto* scene = world.create_scene("Saved room");
    if (!scene)
    {
        return false;
    }
    engine::plugins::modeling::ModelingCommands modeling(world, root);
    engine::editor::SceneCommands scene_commands(world);
    WindowSystem windows;
    RenderSystem render(windows, &world);
    engine::editor::RenderCommands render_commands(render, world, {});
    bool reject_export = false;
    std::size_t export_count = 0;
    std::size_t save_count = 0;
    std::size_t changed_resource_count = 0;
    std::string saved_path;
    engine::editor::ScenePersistence persistence(
        world, root,
        [&](const Node& node, const fs::path& path, std::string& error)
        {
            ++export_count;
            if (reject_export && node.name() == "Second")
            {
                error = "Injected export failure";
                return false;
            }
            return modeling.export_mesh(node, path, error);
        },
        [&](std::string_view path, std::span<const engine::editor::SceneSavedResource> changes)
        {
            ++save_count;
            saved_path = path;
            changed_resource_count = changes.size();
        });
    engine::editor::EditorCommands commands([&]()
                                            { return world.scene() ? world.scene()->id() : 0; });
    if (!modeling.register_commands(commands) || !scene_commands.register_commands(commands) ||
        !persistence.register_commands(commands) ||
        !render_commands.register_commands(commands, scene_commands))
    {
        return false;
    }
    Json result;
    std::string error;
    const auto execute = [&](std::string_view name, const Json& args)
    { return commands.execute(name, args, result, error); };
    const auto id = std::to_string(scene->id());
    const Json identity = {{"sceneId", id}};
    if (!expect(execute("create_node", {{"sceneId", id}, {"name", "Eye"}, {"component", "camera"}}),
                "create camera", error) ||
        !expect(execute("undo", identity) && test_support::scene_nodes(*scene).empty(),
                "undo camera create", error) ||
        !expect(execute("redo", identity), "redo camera create", error))
    {
        return false;
    }
    auto camera = named(*scene, "Eye");
    if (!camera ||
        !expect(execute("set_active_camera",
                        {{"sceneId", id}, {"nodeId", std::to_string(camera.id())}}),
                "select active camera", error) ||
        !expect(execute("undo", identity) && !scene->active_camera(), "undo camera selection",
                error) ||
        !expect(execute("redo", identity) && scene->active_camera(), "redo camera selection",
                error) ||
        !expect(execute("undo", identity) && execute("undo", identity) &&
                    execute("redo", identity) && execute("redo", identity) &&
                    scene->active_camera(),
                "redo camera selection follows recreated node identity", error) ||
        !expect(execute("create_node", {{"sceneId", id}, {"name", "Lamp"}, {"component", "light"}}),
                "create light", error) ||
        !expect(
            !execute("create_node", {{"sceneId", id}, {"name", "Lamp"}, {"component", "empty"}}),
            "duplicate name rejected", error))
    {
        return false;
    }
    camera = named(*scene, "Eye");
    auto light = named(*scene, "Lamp").get_component<Light>();
    auto light_settings = test_support::read_settings(light);
    light_settings.kind = schema::LightKind::point;
    light_settings.intensity = 7;
    if (!light.set_settings(light_settings))
    {
        return false;
    }
    if (!light.set_enabled(false))
    {
        return false;
    }
    for (const char* name : {"First", "Second"})
    {
        if (!expect(
                execute("modeling_create", {{"sceneId", id}, {"shape", "cube"}, {"name", name}}),
                "create editable model", error))
        {
            return false;
        }
    }
    auto first = named(*scene, "First");
    auto second = named(*scene, "Second");
    auto renderer = first.valid() ? first.get_component<MeshRenderer>() : MeshRenderer{};
    float default_roughness = 0;
    if (!first || !second || !renderer ||
        !expect(!renderer.material().has_overrides() &&
                    renderer.material().get_float("roughness", default_roughness) &&
                    renderer.material().set_float("roughness", 0.37F) &&
                    renderer.material().has_overrides() &&
                    renderer.material().set_float("roughness", default_roughness) &&
                    !renderer.material().has_overrides(),
                "material restored to its resource has no persistent overrides") ||
        !expect(renderer.material().set_cull_mode(schema::CullMode::none) &&
                    renderer.material().has_overrides() &&
                    renderer.material().set_cull_mode(schema::CullMode::back) &&
                    !renderer.material().has_overrides(),
                "restoring material coverage clears its persistent override") ||
        !second.set_parent(first) || !renderer.set_material_count(2) ||
        !renderer.material(1).set_v4("baseColor", Vec4(0.2, 0.3, 0.4, 1)) ||
        !renderer.material(1).set_float("roughness", 0.75F) ||
        !renderer.material(1).set_float("alphaCutoff", 0.4F) ||
        !renderer.material(1).set_cull_mode(schema::CullMode::none))
    {
        return false;
    }
    renderer.set_cast_shadows(false);
    const Json receiver = {{"sceneId", id}, {"nodeId", std::to_string(first.id())}};
    auto edit_receiver = receiver;
    edit_receiver.update({{"enabled", false}, {"resolutionScale", .75}, {"clipBias", .04}});
    auto edit_probe = receiver;
    edit_probe.update({{"enabled", false}, {"resource", ""}});
    auto edit_lighting = receiver;
    edit_lighting["bakeStatic"] = true;
    auto edit_blend = receiver;
    edit_blend.update({{"slot", 1}, {"mode", "premultiplied"}});
    if (!expect(execute("set_planar_reflection", edit_receiver) &&
                    execute("set_light_probe_volume", edit_probe) &&
                    execute("set_mesh_lighting", edit_lighting) &&
                    execute("set_material_blend_mode", edit_blend) &&
                    execute("set_render_settings", {{"ssrEnabled", true}, {"ssrMaxSteps", 96}}),
                "edit rendering components and material through the normal commands", error))
    {
        return false;
    }
    auto local_shadow = Json{{"sceneId", id},
                             {"nodeId", std::to_string(light.node().id())},
                             {"castShadows", true},
                             {"shadowResolution", 512},
                             {"shadowBias", .003}};
    if (!expect(execute("set_light", local_shadow), "edit local light shadow settings", error))
    {
        return false;
    }
    const Json material_type = {{"sceneId", id},
                                {"nodeId", std::to_string(first.id())},
                                {"slot", 1},
                                {"path", "builtin://materials/default-unlit"}};
    float restored_roughness = 0;
    if (!expect(execute("set_material_type", material_type) &&
                    renderer.material(1).type_path() == "builtin://materials/default-unlit",
                "switch material resource", error) ||
        !expect(execute("undo", identity) &&
                    renderer.material(1).type_path() == "builtin://materials/default-pbr" &&
                    renderer.material(1).cull_mode() == schema::CullMode::none &&
                    renderer.material(1).blend_mode() == schema::BlendMode::premultiplied &&
                    renderer.material(1).get_float("roughness", restored_roughness) &&
                    restored_roughness == 0.75F,
                "undo material type restores previous overrides", error) ||
        !expect(execute("redo", identity) && execute("undo", identity),
                "material type redo remains reversible", error))
    {
        return false;
    }
    auto transform = test_support::read_settings(first.get_component<Transform>());
    transform.position = Vec3(3, 4, 5);
    if (!first.get_component<Transform>().set_settings(transform))
    {
        return false;
    }
    const Json save = {{"sceneId", id}, {"path", "scenes/room.scene"}};
    auto transient_root = scene->create_node("Generated fragment");
    auto transient_child = scene->create_node("Generated child");
    if (!transient_root || !transient_child || !transient_child.set_parent(transient_root) ||
        !transient_root.add_component<MeshRenderer>())
    {
        return false;
    }
    if (!transient_root.set_transient(true))
    {
        return false;
    }
    const auto history = commands.undo_count();
    if (!expect(execute("save_scene", save) && result["nodeCount"] == 4 &&
                    result["importRequired"] == true && result["resourceCount"] == 4 &&
                    export_count == 2 && commands.undo_count() == history && save_count == 1 &&
                    saved_path == "scenes/room.scene" && changed_resource_count == 4,
                "save full scene without changing live state or undo history", error) ||
        !expect(!execute("save_scene", {{"sceneId", id}, {"path", "../escape.scene"}}),
                "reject traversal", error) ||
        !expect(!execute("save_scene", {{"sceneId", id}, {"path", "/escape.scene"}}),
                "reject absolute save path", error))
    {
        return false;
    }
    const auto path = root / "assets/scenes/room.scene";
    std::string source;
    std::string meta;
    if (!test_support::read_text(path, source) ||
        !test_support::read_text(root / "assets/scenes/room.scene.meta", meta))
    {
        return false;
    }
    schema::SceneDataT saved;
    if (!expect(engine::editor::SceneImporter::read(path, saved, error) &&
                    saved.nodes.size() == 4 && saved.info->active_camera_node == camera.local_id(),
                "saved schema omits transient nodes and descendants, retaining the camera", error))
    {
        return false;
    }
    if (!camera.set_transient(true))
    {
        return false;
    }
    const auto saved_count = save_count;
    if (!expect(!execute("save_scene", save) && save_count == saved_count,
                "transient active camera cannot publish a scene with a dangling reference", error))
    {
        return false;
    }
    if (!camera.set_transient(false))
    {
        return false;
    }
    const auto mesh_path = root / "assets/scenes/room-resources/meshes" /
                           ("node-" + std::to_string(first.local_id()) + ".mesh");
    std::string original_mesh;
    if (!test_support::read_text(mesh_path, original_mesh))
    {
        return false;
    }
    SourceSnapshots initial_sources;
    SourceSnapshots current_sources;
    export_count = 0;
    if (!snapshot_sources(root / "assets", initial_sources) ||
        !expect(execute("save_scene", save) && result["importRequired"] == false &&
                    result["resourceCount"] == 0 && export_count == 0 &&
                    changed_resource_count == 0,
                "unchanged dynamic scene skips mesh exports and imports", error) ||
        !expect(snapshot_sources(root / "assets", current_sources) &&
                    unchanged_sources(initial_sources, current_sources),
                "unchanged save preserves all source and meta timestamps") ||
        !expect(execute("modeling_get", identity), "query dynamic models", error))
    {
        return false;
    }
    const auto models = result["models"];
    for (const auto& model : models)
    {
        const Json edit = {
            {"sceneId", id},
            {"modelId", model["modelId"]},
            {"operation", "transform"},
            {"selection", {{"all", true}}},
            {"parameters", {{"matrix", {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.25, 0, 0, 1}}}}};
        if (!expect(execute("modeling_edit", edit), "change mesh before failure injection", error))
        {
            return false;
        }
    }
    reject_export = true;
    const auto saves_before_failure = save_count;
    if (!expect(!execute("save_scene", save) && save_count == saves_before_failure,
                "injected failure rolls back complete save without a saved notification", error))
    {
        return false;
    }
    reject_export = false;
    export_count = 0;
    std::string restored;
    if (!expect(test_support::read_text(path, restored) && restored == source,
                "scene restored after failed save") ||
        !expect(test_support::read_text(mesh_path, restored) && restored == original_mesh,
                "earlier mesh publication restored after later failure") ||
        !expect(execute("save_scene", save) && result["resourceCount"] == 2 && export_count == 2,
                "failed save does not advance mesh snapshots before successful retry", error) ||
        !expect(test_support::read_text(root / "assets/scenes/room.scene.meta", restored) &&
                    restored == meta,
                "repeat save preserves scene GUID"))
    {
        return false;
    }
    auto mesh_meta_path = mesh_path;
    mesh_meta_path += ".meta";
    const auto material_path = root / "assets/scenes/room-resources/materials" /
                               ("node-" + std::to_string(first.local_id()) + "-slot-1.material");
    for (const auto& conflict_path : {mesh_path, mesh_meta_path, material_path, path})
    {
        std::string original;
        if (!test_support::read_text(conflict_path, original) ||
            !write_text(conflict_path, original + "\n# external modification\n") ||
            !snapshot_sources(root / "assets", initial_sources))
        {
            return false;
        }
        const auto saves_before_conflict = save_count;
        export_count = 0;
        if (!expect(!execute("save_scene", save) && export_count == 0 &&
                        save_count == saves_before_conflict &&
                        snapshot_sources(root / "assets", current_sources) &&
                        unchanged_sources(initial_sources, current_sources),
                    "external source or metadata changes reject save without rewriting files",
                    error) ||
            !write_text(conflict_path, original) ||
            !expect(execute("save_scene", save) && result["resourceCount"] == 0 &&
                        result["importRequired"] == false && export_count == 0 &&
                        changed_resource_count == 0,
                    "restoring external changes resumes unchanged save without advancing snapshots",
                    error))
        {
            return false;
        }
    }
    std::string mesh_meta;
    std::error_code ec;
    if (!test_support::read_text(mesh_meta_path, mesh_meta) || !fs::remove(mesh_meta_path, ec) ||
        ec ||
        !expect(!execute("save_scene", save), "missing saved mesh identity is not regenerated",
                error) ||
        !write_text(mesh_meta_path, mesh_meta) ||
        !expect(execute("save_scene", save) && result["resourceCount"] == 0 &&
                    result["importRequired"] == false,
                "restoring source metadata allows the unchanged save to resume", error))
    {
        return false;
    }
    commands.clear_history();
    world.destroy_scene();
    modeling.synchronize_scene();
    if (!expect(pipeline.build_assets(root), "import saved scene and dependencies",
                pipeline.last_error()) ||
        !expect(resources.update_catalog(pipeline.catalog(), {{"content", root / "cache"}}),
                "publish imported resource catalog", std::string(resources.last_error())))
    {
        return false;
    }
    if (!services.prepare_builtins() || !services.load("scenes/room.scene"))
    {
        return false;
    }
    scene = world.load_scene("scenes/room.scene");
    if (!expect(scene && test_support::scene_nodes(*scene).size() == 4 && scene->active_camera() &&
                    scene->active_camera().node().name() == "Eye",
                "reload complete scene"))
    {
        return false;
    }
    first = named(*scene, "First");
    second = named(*scene, "Second");
    renderer = first.get_component<MeshRenderer>();
    Vec4 color;
    float roughness = 0;
    float cutoff = 0;
    if (!expect(second.parent() == first &&
                    test_support::read_settings(first.get_component<Transform>()).position.x() ==
                        3 &&
                    renderer.material_count() == 2 && !renderer.cast_shadows() &&
                    renderer.bake_static() &&
                    renderer.material(1).blend_mode() == schema::BlendMode::premultiplied &&
                    first.get_component<PlanarReflectionComponent>() &&
                    !first.get_component<PlanarReflectionComponent>().enabled() &&
                    test_support::read_settings(first.get_component<PlanarReflectionComponent>())
                            .resolution_scale == .75F &&
                    test_support::read_settings(first.get_component<PlanarReflectionComponent>())
                            .clip_bias == .04F &&
                    first.get_component<LightProbeVolumeComponent>() &&
                    !first.get_component<LightProbeVolumeComponent>().enabled() &&
                    scene->active_camera().node().get_component<PostProcessComponent>() &&
                    test_support::read_settings(
                        scene->active_camera().node().get_component<PostProcessComponent>())
                        .ssr_enabled &&
                    test_support::read_settings(
                        scene->active_camera().node().get_component<PostProcessComponent>())
                            .ssr_max_steps == 96 &&
                    renderer.material(1).cull_mode() == schema::CullMode::none &&
                    renderer.material(1).get_float("alphaCutoff", cutoff) && cutoff == 0.4F &&
                    renderer.material(1).get_v4("baseColor", color) && color.y() == 0.3 &&
                    renderer.material(1).get_float("roughness", roughness) && roughness == 0.75F,
                "reload hierarchy, transform and all material slot values") ||
        !expect(!named(*scene, "Lamp").get_component<Light>().enabled() &&
                    test_support::read_settings(named(*scene, "Lamp").get_component<Light>())
                            .intensity == 7 &&
                    test_support::read_settings(named(*scene, "Lamp").get_component<Light>())
                        .cast_shadows &&
                    test_support::read_settings(named(*scene, "Lamp").get_component<Light>())
                            .shadow_resolution == 512 &&
                    test_support::read_settings(named(*scene, "Lamp").get_component<Light>())
                            .shadow_bias == .003F,
                "reload light values and disabled component"))
    {
        return false;
    }
    modeling.synchronize_scene();
    const Json current = {{"sceneId", std::to_string(scene->id())}};
    if (!expect(execute("modeling_get", current) && result["models"].size() == 2 &&
                    result["models"][0]["faceCount"] == 6,
                "reload preserves polygon editing sources", error))
    {
        return false;
    }
    auto edit = current;
    edit.update({{"modelId", result["models"][0]["modelId"]},
                 {"operation", "inset"},
                 {"selection", {{"all", true}}},
                 {"parameters", {{"distance", 0.05}}}});
    if (!expect(execute("modeling_edit", edit), "loaded model remains editable", error) ||
        !expect(execute("undo", current), "loaded model edit is undoable", error))
    {
        return false;
    }
    // 切回资源引用，避免建模撤销保留的瞬态网格影响普通加载场景的回归。
    if (!expect(renderer.set_mesh("scenes/room-resources/meshes/node-" +
                                  std::to_string(first.local_id()) + ".mesh"),
                "restore imported mesh resource after modeling round trip"))
    {
        return false;
    }
    const Json loaded_save = {{"sceneId", std::to_string(scene->id())},
                              {"path", "scenes/room.scene"}};
    export_count = 0;
    if (!snapshot_sources(root / "assets", initial_sources) ||
        !expect(execute("save_scene", loaded_save) && result["resourceCount"] == 0 &&
                    result["importRequired"] == false && export_count == 0,
                "loaded scene preserves imported mesh and material references", error) ||
        !expect(snapshot_sources(root / "assets", current_sources) &&
                    unchanged_sources(initial_sources, current_sources),
                "loaded unchanged scene creates no per-node copies"))
    {
        return false;
    }
    const std::string original_material_path(renderer.material(1).type_path());
    const auto original_material_source =
        root / "assets" / PathUtils::from_utf8(original_material_path);
    std::string original_material;
    if (!test_support::read_text(original_material_source, original_material))
    {
        return false;
    }
    Guid shared_mesh_guid;
    Guid shared_material_guid;
    if (!resources.catalog().identities().path_to_guid(renderer.mesh_path(), shared_mesh_guid) ||
        !resources.catalog().identities().path_to_guid(original_material_path,
                                                       shared_material_guid))
    {
        return false;
    }
    Node shared = {};
    for (std::size_t index = 0; index != 6; ++index)
    {
        auto node = scene->create_node("Shared " + std::to_string(index));
        auto mesh = node ? node.add_component<MeshRenderer>() : MeshRenderer{};
        if (!mesh || !mesh.set_mesh(renderer.mesh_path()) ||
            !mesh.material().set_type(original_material_path))
        {
            return false;
        }
        shared = node;
    }
    auto environment = named(*scene, "Lamp").add_component<EnvironmentComponent>();
    if (!environment ||
        !expect(execute("save_scene", loaded_save) && result["resourceCount"] == 1 &&
                    export_count == 0 && engine::editor::SceneImporter::read(path, saved, error),
                "many nodes sharing imported assets save only the scene", error))
    {
        return false;
    }
    const auto* shared_saved = saved_mesh(saved, shared.local_id());
    if (!expect(shared_saved && shared_saved->mesh_guid == shared_mesh_guid &&
                    shared_saved->material_guid == shared_material_guid,
                "shared resources keep their original GUIDs") ||
        !expect(snapshot_sources(root / "assets", current_sources) &&
                    unchanged_sources(initial_sources, current_sources, path),
                "shared nodes do not create mesh or material source duplicates"))
    {
        return false;
    }
    initial_sources = current_sources;
    const std::string before_transform = current_sources.at(path).content;
    const Json move = {{"sceneId", std::to_string(scene->id())},
                       {"nodeId", std::to_string(first.id())},
                       {"position", {9, 4, 5}}};
    if (!expect(execute("set_transform", move), "edit loaded scene transform", error))
    {
        return false;
    }
    const auto edited_history = commands.undo_count();
    if (!expect(execute("save_scene", loaded_save) && result["resourceCount"] == 1 &&
                    commands.undo_count() == edited_history && export_count == 0,
                "transform save preserves the editing undo stack", error) ||
        !expect(snapshot_sources(root / "assets", current_sources) &&
                    unchanged_sources(initial_sources, current_sources, path),
                "transform changes touch only scene source"))
    {
        return false;
    }
    const std::string after_transform = current_sources.at(path).content;
    if (!expect(execute("undo", current) && execute("save_scene", loaded_save) &&
                    result["resourceCount"] == 1 && commands.redo_count() == 1 &&
                    test_support::read_text(path, restored) && restored == before_transform,
                "saved edit can be undone and the reverted scene can be saved", error) ||
        !expect(execute("redo", current) && execute("save_scene", loaded_save) &&
                    result["resourceCount"] == 1 && commands.undo_count() == edited_history &&
                    test_support::read_text(path, restored) && restored == after_transform &&
                    export_count == 0,
                "saving after undo preserves redo and the original resource identities", error))
    {
        return false;
    }
    const Json material_edit = {{"sceneId", std::to_string(scene->id())},
                                {"nodeId", std::to_string(first.id())},
                                {"slot", 1},
                                {"name", "roughness"},
                                {"type", "float"},
                                {"value", 0.45}};
    if (!expect(execute("set_material_parameter", material_edit) &&
                    execute("save_scene", loaded_save) && result["resourceCount"] == 2 &&
                    export_count == 0 && engine::editor::SceneImporter::read(path, saved, error),
                "one local material override writes only its source and the scene", error))
    {
        return false;
    }
    const auto* first_saved = saved_mesh(saved, first.local_id());
    shared_saved = saved_mesh(saved, shared.local_id());
    if (!expect(first_saved && first_saved->additional_material_guids.size() == 1 &&
                    first_saved->additional_material_guids[0] != shared_material_guid &&
                    shared_saved && shared_saved->material_guid == shared_material_guid &&
                    test_support::read_text(original_material_source, restored) &&
                    restored == original_material,
                "local override does not overwrite the material used by other nodes") ||
        !snapshot_sources(root / "assets", initial_sources) ||
        !expect(execute("save_scene", loaded_save) && result["resourceCount"] == 0 &&
                    result["importRequired"] == false && export_count == 0 &&
                    snapshot_sources(root / "assets", current_sources) &&
                    unchanged_sources(initial_sources, current_sources),
                "repeated override save preserves all timestamps", error) ||
        !expect(execute("undo", current) && !renderer.material(1).has_overrides() &&
                    execute("save_scene", loaded_save) && result["resourceCount"] == 1 &&
                    commands.redo_count() == 1,
                "undoing the override restores the original material reference", error) ||
        !expect(execute("redo", current) && execute("save_scene", loaded_save) &&
                    result["resourceCount"] == 1 && export_count == 0,
                "redo reuses the previously saved override resource", error))
    {
        return false;
    }
    if (!snapshot_sources(root / "assets", initial_sources))
    {
        return false;
    }
    auto environment_settings = test_support::read_settings(environment);
    environment_settings.ambient_intensity = 0.4F;
    light = named(*scene, "Lamp").get_component<Light>();
    light_settings = test_support::read_settings(light);
    light_settings.intensity = 3;
    if (!expect(environment.set_settings(environment_settings) &&
                    light.set_settings(light_settings) && execute("save_scene", loaded_save) &&
                    result["resourceCount"] == 1 && export_count == 0 &&
                    snapshot_sources(root / "assets", current_sources) &&
                    unchanged_sources(initial_sources, current_sources, path),
                "lighting and environment edits do not export referenced assets", error))
    {
        return false;
    }
    auto shared_renderer = shared.get_component<MeshRenderer>();
    if (!shared_renderer.set_material_count(2) ||
        !shared_renderer.material(1).set_type(original_material_path))
    {
        return false;
    }
    const Json prefab_create = {{"sceneId", std::to_string(scene->id())},
                                {"nodeId", std::to_string(shared.id())},
                                {"path", "prefabs/material.prefab"}};
    if (!expect(execute("create_prefab", prefab_create) && pipeline.build_assets(root) &&
                    resources.update_catalog(pipeline.catalog(), {{"content", root / "cache"}}) &&
                    services.prepare_builtins() &&
                    execute("instantiate_prefab", {{"sceneId", std::to_string(scene->id())},
                                                   {"path", "prefabs/material.prefab"},
                                                   {"name", "Prefab copy"}}),
                "subtree prefab enters the standard import and instantiate path", error))
    {
        return false;
    }
    auto prefab_node = named(*scene, "Prefab copy");
    auto prefab_renderer = prefab_node ? prefab_node.get_component<MeshRenderer>() : MeshRenderer{};
    if (!prefab_renderer)
    {
        return false;
    }
    auto prefab_material = current;
    prefab_material.update({{"nodeId", std::to_string(prefab_node.id())},
                            {"slot", 0},
                            {"name", "roughness"},
                            {"type", "float"},
                            {"value", 0.29}});
    if (!expect(execute("set_material_parameter", prefab_material),
                "prefab primary material edit becomes a binding override", error))
    {
        return false;
    }
    prefab_material["slot"] = 1;
    prefab_material["value"] = 0.37;
    float primary = 0, additional = 0;
    const Json restore_binding = {{"sceneId", std::to_string(scene->id())},
                                  {"nodeId", std::to_string(prefab_node.id())},
                                  {"componentType", "MeshRendererData"},
                                  {"fields", {"material"}}};
    if (!expect(execute("set_material_parameter", prefab_material) &&
                    execute("revert_prefab_override", restore_binding) &&
                    prefab_renderer.material().get_float("roughness", primary) &&
                    prefab_renderer.material(1).get_float("roughness", additional) &&
                    primary == 0.75F && additional == 0.37F && execute("undo", current) &&
                    prefab_renderer.material().get_float("roughness", primary) &&
                    prefab_renderer.material(1).get_float("roughness", additional) &&
                    primary == 0.29F && additional == 0.37F,
                "material restore preserves other slots and undo restores complete parameters",
                error))
    {
        return false;
    }
    const Json prefab_save = {{"sceneId", std::to_string(scene->id())},
                              {"path", "scenes/prefab.scene"}};
    schema::SceneDataT prefab_saved;
    if (!expect(execute("save_scene", prefab_save) &&
                    engine::editor::SceneImporter::read(root / "assets/scenes/prefab.scene",
                                                        prefab_saved, error) &&
                    prefab_saved.prefab_instances.size() == 1,
                "save preserves prefab association with independent material exports", error))
    {
        return false;
    }
    const schema::MeshRendererDataT* material_override = nullptr;
    for (const auto& node_override : prefab_saved.prefab_instances.front()->overrides)
    {
        for (const auto& component_override : node_override->components)
        {
            if (const auto mesh = component_override->component->payload.AsMeshRendererData())
            {
                material_override = mesh;
            }
        }
    }
    if (!expect(material_override && material_override->material_guid != shared_material_guid &&
                    material_override->additional_material_guids.size() == 1 &&
                    material_override->additional_material_guids.front() != shared_material_guid &&
                    test_support::read_text(original_material_source, restored) &&
                    restored == original_material,
                "prefab overrides export full material resources without changing the template",
                error))
    {
        return false;
    }
    commands.clear_history();
    world.shutdown();
    resources.shutdown();
    pipeline.shutdown();
    return true;
}
} // namespace

int main()
{
    std::error_code ec;
    auto root = fs::temp_directory_path(ec);
    if (ec)
    {
        return 1;
    }
    root /= "engine-scene-persistence-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    for (const char* directory : {"assets", "settings", "cache"})
    {
        fs::create_directories(root / directory, ec);
        if (ec)
        {
            return 1;
        }
    }
    const bool success =
        write_text(root / "settings/editor-import.toml", "[common]\nimport_threads = 8\n") &&
        run(root) && retained_shader_save(root);
    fs::remove_all(root, ec);
    return success ? 0 : 1;
}
