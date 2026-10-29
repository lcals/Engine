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
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/world/world_system.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

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
    engine::editor::ScenePersistence persistence(
        world, root,
        [&](const Node& node, const fs::path& path, std::string& error)
        {
            if (reject_export && node.name() == "Second")
            {
                error = "Injected export failure";
                return false;
            }
            return modeling.export_mesh(node, path, error);
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
    auto* renderer = first->get_component<MeshRenderer>();
    if (!first || !second || !second->set_parent(first) || !renderer->set_material_count(2) ||
        !renderer->material(1)->set_v4("baseColor", Vec4(0.2, 0.3, 0.4, 1)) ||
        !renderer->material(1)->set_float("roughness", 0.75F))
    {
        return false;
    }
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
                    result["importRequired"] == true && commands.undo_count() == history,
                "save full scene without changing live state or undo history", error) ||
        !expect(!execute("save_scene", {{"sceneId", id}, {"path", "../escape.scene"}}),
                "reject traversal", error))
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
    reject_export = true;
    if (!expect(!execute("save_scene", save), "injected failure rolls back complete save", error))
    {
        return false;
    }
    reject_export = false;
    std::string restored;
    if (!expect(ResourceSystem::read_text(path, restored) && restored == source,
                "scene restored after failed save") ||
        !expect(ResourceSystem::read_text(mesh_path, restored) && restored == original_mesh,
                "earlier mesh publication restored after later failure") ||
        !expect(execute("save_scene", save), "repeat save", error) ||
        !expect(ResourceSystem::read_text(root / "assets/scenes/room.scene.meta", restored) &&
                    restored == meta,
                "repeat save preserves scene GUID"))
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
    if (!expect(second->parent() == first &&
                    first->get_component<Transform>()->settings().position.x() == 3 &&
                    renderer->material_count() == 2 &&
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
