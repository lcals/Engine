#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/scene_commands.hpp"
#include "editor/project/project_system.hpp"
#include "editor/scene/scene_persistence.hpp"
#include "plugins/editor/modeling/modeling_commands.hpp"
#include "plugins/editor/modeling/modeling_mesh_importer.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/world/world_system.hpp"

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

Node* named(Scene& scene, std::string_view name)
{
    for (const auto& node : scene.nodes())
    {
        if (node->name() == name)
        {
            return node.get();
        }
    }
    return nullptr;
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
            if (ec || !ResourceSystem::read_text(current->path(), snapshot.content))
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

const schema::MeshRendererDataT* saved_mesh(const schema::SceneDataT& scene,
                                          std::uint64_t node_id)
{
    for (const auto& node : scene.nodes)
    {
        if (!node || !node->info || node->info->id != node_id)
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

bool run(const fs::path& root)
{
    // 与 editor_app 同样装配项目系统，捕获跨模块 TOML ABI/符号冲突。
    engine::editor::ProjectSystem project(PathUtils::to_utf8(root));
    ResourceSystem resources;
    WorldSystem world;
    engine::editor::AssetPipelineSystem pipeline;
    if (!project.initialize({}) || !resources.initialize({}) || !world.initialize({}) ||
        !pipeline.initialize({}))
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
        !persistence.register_commands(commands))
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
        !expect(execute("undo", identity) && scene->nodes().empty(), "undo camera create", error) ||
        !expect(execute("redo", identity), "redo camera create", error))
    {
        return false;
    }
    auto* camera = named(*scene, "Eye");
    if (!camera ||
        !expect(execute("set_active_camera",
                        {{"sceneId", id}, {"nodeId", std::to_string(camera->id())}}),
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
    auto* light = named(*scene, "Lamp")->get_component<Light>();
    auto light_settings = light->settings();
    light_settings.kind = schema::LightKind::point;
    light_settings.intensity = 7;
    if (!light->set_settings(light_settings))
    {
        return false;
    }
    light->set_enabled(false);
    for (const char* name : {"First", "Second"})
    {
        if (!expect(
                execute("modeling_create", {{"sceneId", id}, {"shape", "cube"}, {"name", name}}),
                "create editable model", error))
        {
            return false;
        }
    }
    auto* first = named(*scene, "First");
    auto* second = named(*scene, "Second");
    auto* renderer = first != nullptr ? first->get_component<MeshRenderer>() : nullptr;
    float default_roughness = 0;
    if (!first || !second || !renderer ||
        !expect(!renderer->material().has_overrides() &&
                    renderer->material().get_float("roughness", default_roughness) &&
                    renderer->material().set_float("roughness", 0.37F) &&
                    renderer->material().has_overrides() &&
                    renderer->material().set_float("roughness", default_roughness) &&
                    !renderer->material().has_overrides(),
                "material restored to its resource has no persistent overrides") ||
        !expect(renderer->material().set_cull_mode(schema::CullMode::none) &&
                    renderer->material().has_overrides() &&
                    renderer->material().set_cull_mode(schema::CullMode::back) &&
                    !renderer->material().has_overrides(),
                "restoring material coverage clears its persistent override") ||
        !second->set_parent(first) ||
        !renderer->set_material_count(2) ||
        !renderer->material(1)->set_v4("baseColor", Vec4(0.2, 0.3, 0.4, 1)) ||
        !renderer->material(1)->set_float("roughness", 0.75F) ||
        !renderer->material(1)->set_float("alphaCutoff", 0.4F) ||
        !renderer->material(1)->set_cull_mode(schema::CullMode::none))
    {
        return false;
    }
    renderer->set_cast_shadows(false);
    const Json material_type = {{"sceneId", id},
                                {"nodeId", std::to_string(first->id())},
                                {"slot", 1},
                                {"path", "builtin/materials/default-unlit"}};
    float restored_roughness = 0;
    if (!expect(execute("set_material_type", material_type) &&
                    renderer->material(1)->type_path() == "builtin/materials/default-unlit",
                "switch material resource", error) ||
        !expect(execute("undo", identity) &&
                    renderer->material(1)->type_path() == "builtin/materials/default-pbr" &&
                    renderer->material(1)->cull_mode() == schema::CullMode::none &&
                    renderer->material(1)->get_float("roughness", restored_roughness) &&
                    restored_roughness == 0.75F,
                "undo material type restores previous overrides", error) ||
        !expect(execute("redo", identity) && execute("undo", identity),
                "material type redo remains reversible", error))
    {
        return false;
    }
    auto transform = first->get_component<Transform>()->settings();
    transform.position = Vec3(3, 4, 5);
    if (!first->get_component<Transform>()->set_settings(transform))
    {
        return false;
    }
    const Json save = {{"sceneId", id}, {"path", "scenes/room.scene"}};
    const auto history = commands.undo_count();
    if (!expect(execute("save_scene", save) && result["nodeCount"] == 4 &&
                    result["importRequired"] == true && result["resourceCount"] == 4 &&
                    export_count == 2 && commands.undo_count() == history && save_count == 1 &&
                    saved_path == "project/scenes/room.scene" && changed_resource_count == 4,
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
    if (!ResourceSystem::read_text(path, source) ||
        !ResourceSystem::read_text(root / "assets/scenes/room.scene.meta", meta))
    {
        return false;
    }
    schema::SceneDataT saved;
    if (!expect(engine::editor::SceneImporter::read(path, saved, error) &&
                    saved.nodes.size() == 4 && saved.info->active_camera_node == camera->info().id,
                "saved schema contains camera and all nodes", error))
    {
        return false;
    }
    const auto mesh_path = root / "assets/scenes/room-resources/meshes" /
                           ("node-" + std::to_string(first->info().id) + ".mesh");
    std::string original_mesh;
    if (!ResourceSystem::read_text(mesh_path, original_mesh))
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
    if (!expect(ResourceSystem::read_text(path, restored) && restored == source,
                "scene restored after failed save") ||
        !expect(ResourceSystem::read_text(mesh_path, restored) && restored == original_mesh,
                "earlier mesh publication restored after later failure") ||
        !expect(execute("save_scene", save) && result["resourceCount"] == 2 && export_count == 2,
                "failed save does not advance mesh snapshots before successful retry", error) ||
        !expect(ResourceSystem::read_text(root / "assets/scenes/room.scene.meta", restored) &&
                    restored == meta,
                "repeat save preserves scene GUID"))
    {
        return false;
    }
    auto mesh_meta_path = mesh_path;
    mesh_meta_path += ".meta";
    const auto material_path = root / "assets/scenes/room-resources/materials" /
                               ("node-" + std::to_string(first->info().id) + "-slot-1.material");
    for (const auto& conflict_path : {mesh_path, mesh_meta_path, material_path, path})
    {
        std::string original;
        if (!ResourceSystem::read_text(conflict_path, original) ||
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
    if (!ResourceSystem::read_text(mesh_meta_path, mesh_meta) ||
        !fs::remove(mesh_meta_path, ec) || ec ||
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
                "publish imported resource catalog", resources.last_error()))
    {
        return false;
    }
    scene = world.load_scene("project/scenes/room.scene");
    if (!expect(scene && scene->nodes().size() == 4 && scene->active_camera() &&
                    scene->active_camera()->node().name() == "Eye",
                "reload complete scene"))
    {
        return false;
    }
    first = named(*scene, "First");
    second = named(*scene, "Second");
    renderer = first->get_component<MeshRenderer>();
    Vec4 color;
    float roughness = 0;
    float cutoff = 0;
    if (!expect(second->parent() == first &&
                    first->get_component<Transform>()->settings().position.x() == 3 &&
                    renderer->material_count() == 2 &&
                    !renderer->cast_shadows() &&
                    renderer->material(1)->cull_mode() == schema::CullMode::none &&
                    renderer->material(1)->get_float("alphaCutoff", cutoff) && cutoff == 0.4F &&
                    renderer->material(1)->get_v4("baseColor", color) && color.y() == 0.3 &&
                    renderer->material(1)->get_float("roughness", roughness) && roughness == 0.75F,
                "reload hierarchy, transform and all material slot values") ||
        !expect(!named(*scene, "Lamp")->get_component<Light>()->enabled() &&
                    named(*scene, "Lamp")->get_component<Light>()->settings().intensity == 7,
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
    if (!expect(renderer->set_mesh("project/scenes/room-resources/meshes/node-" +
                                   std::to_string(first->info().id) + ".mesh"),
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
    const std::string original_material_path(renderer->material(1)->type_path());
    const auto original_material_source =
        root / "assets" / PathUtils::from_utf8(original_material_path.substr(8));
    std::string original_material;
    if (!ResourceSystem::read_text(original_material_source, original_material))
    {
        return false;
    }
    Guid shared_mesh_guid;
    Guid shared_material_guid;
    if (!ResourceSystem::path_to_guid(renderer->mesh_path(), shared_mesh_guid) ||
        !ResourceSystem::path_to_guid(original_material_path, shared_material_guid))
    {
        return false;
    }
    Node* shared = nullptr;
    for (std::size_t index = 0; index != 6; ++index)
    {
        auto* node = scene->create_node("Shared " + std::to_string(index));
        auto* mesh = node ? node->add_component<MeshRenderer>() : nullptr;
        if (!mesh || !mesh->set_mesh(renderer->mesh_path()) ||
            !mesh->material().set_type(original_material_path))
        {
            return false;
        }
        shared = node;
    }
    auto* environment = named(*scene, "Lamp")->add_component<EnvironmentComponent>();
    if (!environment ||
        !expect(execute("save_scene", loaded_save) && result["resourceCount"] == 1 &&
                    export_count == 0 && engine::editor::SceneImporter::read(path, saved, error),
                "many nodes sharing imported assets save only the scene", error))
    {
        return false;
    }
    const auto* shared_saved = saved_mesh(saved, shared->info().id);
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
                       {"nodeId", std::to_string(first->id())},
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
                    ResourceSystem::read_text(path, restored) && restored == before_transform,
                "saved edit can be undone and the reverted scene can be saved", error) ||
        !expect(execute("redo", current) && execute("save_scene", loaded_save) &&
                    result["resourceCount"] == 1 && commands.undo_count() == edited_history &&
                    ResourceSystem::read_text(path, restored) && restored == after_transform &&
                    export_count == 0,
                "saving after undo preserves redo and the original resource identities", error))
    {
        return false;
    }
    const Json material_edit = {{"sceneId", std::to_string(scene->id())},
                                {"nodeId", std::to_string(first->id())},
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
    const auto* first_saved = saved_mesh(saved, first->info().id);
    shared_saved = saved_mesh(saved, shared->info().id);
    if (!expect(first_saved && first_saved->additional_material_guids.size() == 1 &&
                    first_saved->additional_material_guids[0] != shared_material_guid &&
                    shared_saved && shared_saved->material_guid == shared_material_guid &&
                    ResourceSystem::read_text(original_material_source, restored) &&
                    restored == original_material,
                "local override does not overwrite the material used by other nodes") ||
        !snapshot_sources(root / "assets", initial_sources) ||
        !expect(execute("save_scene", loaded_save) && result["resourceCount"] == 0 &&
                    result["importRequired"] == false && export_count == 0 &&
                    snapshot_sources(root / "assets", current_sources) &&
                    unchanged_sources(initial_sources, current_sources),
                "repeated override save preserves all timestamps", error) ||
        !expect(execute("undo", current) && !renderer->material(1)->has_overrides() &&
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
    auto environment_settings = environment->settings();
    environment_settings.ambient_intensity = 0.4F;
    light = named(*scene, "Lamp")->get_component<Light>();
    light_settings = light->settings();
    light_settings.intensity = 3;
    if (!expect(environment->set_settings(environment_settings) &&
                    light->set_settings(light_settings) && execute("save_scene", loaded_save) &&
                    result["resourceCount"] == 1 && export_count == 0 &&
                    snapshot_sources(root / "assets", current_sources) &&
                    unchanged_sources(initial_sources, current_sources, path),
                "lighting and environment edits do not export referenced assets", error))
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
    const bool success = run(root);
    fs::remove_all(root, ec);
    return success ? 0 : 1;
}
