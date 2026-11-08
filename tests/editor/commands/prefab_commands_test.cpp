#include "resource_fixture.hpp"
#include "resource_services.hpp"
#include "scene_snapshot.hpp"

#include "editor/asset_pipeline/prefab_importer.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/scene_commands.hpp"
#include "editor/scene/scene_persistence.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

namespace
{
using namespace engine;
using Json = nlohmann::json;
namespace fs = std::filesystem;

bool expect(bool condition, const char* text, const std::string& error = {})
{
    if (!condition)
    {
        std::fprintf(stderr, "prefab commands: %s: %s\n", text, error.c_str());
    }
    return condition;
}

std::unique_ptr<schema::NodeDataT> node_data(std::uint64_t id, std::uint64_t parent)
{
    auto node = std::make_unique<schema::NodeDataT>();
    node->info = test_support::node_info();
    node->info->properties->id = id;
    node->info->parent_id = parent;
    node->info->properties->name = parent == 0 ? "Template" : "Lamp";
    auto transform = test_support::component_data();
    transform->payload.Set(schema::TransformDataT{});
    node->components.push_back(std::move(transform));
    return node;
}

bool run()
{
    test_support::ResourceFixture files;
    test_support::ResourceServices services;
    engine::ResourceSystem& resources = services.core;
    engine::WorldSystem world{&services.loader};
    if (!(services.initialize({}) && services.prepare_builtins()) || !world.initialize({}))
    {
        return false;
    }
    std::error_code code;
    for (const auto* directory : {"settings", "cache", "assets"})
    {
        fs::create_directories(files.root / directory, code);
        if (code)
        {
            return false;
        }
    }
    schema::PrefabDataT source;
    source.schema_version = 2;
    source.root_id = 9;
    // 根不要求排在源数组首项；撤销必须删除实际实例根。
    source.nodes.push_back(node_data(20, 9));
    auto light = test_support::component_data();
    light->payload.Set(schema::LightDataT{});
    source.nodes.front()->components.push_back(std::move(light));
    source.nodes.push_back(node_data(9, 0));
    if (!files.add("unit.prefab", source) ||
        !(files.publish(resources) && services.prepare_builtins()))
    {
        return false;
    }
    auto* scene = world.create_scene("Prefab edits");
    if (!scene)
    {
        return false;
    }
    editor::SceneCommands module(world);
    editor::ScenePersistence persistence(world, files.root, {});
    editor::EditorCommands commands([&world]() { return world.scene()->id(); });
    if (!module.register_commands(commands) || !persistence.register_commands(commands))
    {
        return false;
    }
    Json result;
    std::string error;
    const auto execute = [&](std::string_view name, const Json& arguments)
    { return commands.execute(name, arguments, result, error); };
    const Json identity{{"sceneId", std::to_string(scene->id())}};
    auto request = identity;
    request.update({{"path", "unit.prefab"}, {"name", "First"}});
    if (!expect(execute("instantiate_prefab", request) &&
                    test_support::scene_nodes(*scene).size() == 2,
                "instantiate through editor", error))
    {
        return false;
    }
    auto child = test_support::scene_nodes(*scene).front();
    auto root = test_support::scene_nodes(*scene).back();
    const auto child_identity = module.node_identity(child.id());
    const auto root_identity = module.node_identity(root.id());
    const auto target = [&]()
    {
        Json value = identity;
        value["nodeId"] = std::to_string(*child_identity);
        return value;
    };
    auto edit = target();
    edit["position"] = {0, 0, 0};
    if (!expect(execute("set_transform", edit), "same-value edit is recorded", error))
    {
        return false;
    }
    edit = target();
    edit.update({{"componentType", "LightData"}, {"enabled", false}, {"visible", false}});
    if (!expect(execute("set_component_state", edit), "false component state is explicit", error))
    {
        return false;
    }
    edit = target();
    edit["enabled"] = false;
    if (!expect(execute("set_node_state", edit), "false node state is explicit", error))
    {
        return false;
    }
    edit = target();
    edit.update({{"componentType", "TransformData"}, {"fields", {"scale"}}});
    if (!expect(execute("set_prefab_override", edit), "explicit current-value mask", error))
    {
        return false;
    }
    if (!expect(execute("get_prefab_instance", target()) && result["overrides"].size() == 2,
                "query includes root name and child masks", error))
    {
        return false;
    }
    edit = target();
    edit.update({{"componentType", "LightData"}, {"fields", {"enabled"}}});
    if (!expect(execute("revert_prefab_override", edit) && child.get_component<Light>().enabled() &&
                    !child.get_component<Light>().visible(),
                "restore selected field preserves independent false override", error))
    {
        return false;
    }
    const auto history_count = commands.undo_count();
    for (std::size_t index = 0; index < history_count; ++index)
    {
        if (!expect(execute("undo", identity), "undo the entire prefab history", error))
        {
            return false;
        }
    }
    if (!expect(test_support::scene_nodes(*scene).empty() &&
                    test_support::prefab_instances(*scene).empty(),
                "undo removes child-before-root prefab completely", error))
    {
        return false;
    }
    for (std::size_t index = 0; index < history_count; ++index)
    {
        if (!expect(execute("redo", identity), "redo rebases shared node identities and masks",
                    error))
        {
            return false;
        }
    }
    child = scene->find_node(*child_identity);
    root = scene->find_node(*root_identity);
    if (!expect(child && root && !child.enabled() && child.get_component<Light>().enabled() &&
                    !child.get_component<Light>().visible(),
                "redo preserves selected state", error))
    {
        return false;
    }
    auto save = identity;
    save["path"] = "saved.scene";
    if (!expect(execute("save_scene", save), "save prefab source association", error))
    {
        return false;
    }
    schema::SceneDataT saved;
    if (!expect(editor::SceneImporter::read(files.root / "assets/saved.scene", saved, error) &&
                    saved.nodes.empty() && saved.prefab_instances.size() == 1,
                "scene source stores association instead of flattened template nodes", error))
    {
        return false;
    }
    // 未覆盖字段跟随新模板，同值position/scale与false状态继续保留。
    auto updated = source;
    auto* transform = updated.nodes.front()->components.front()->payload.AsTransformData();
    transform->position = {8, 0, 0};
    transform->scale = {2, 2, 2};
    transform->rotation = {0, 60, 0};
    updated.nodes.front()->components.back()->payload.AsLightData()->intensity = 5;
    if (!expect(editor::PrefabImporter::expand_instance(updated, *saved.prefab_instances.front(),
                                                        1000, saved.nodes, error),
                "recompose saved overrides against updated source", error))
    {
        return false;
    }
    const auto expanded =
        std::find_if(saved.nodes.begin(), saved.nodes.end(), [child](const auto& value)
                     { return value->info->properties->id == child.local_id(); });
    if (!expect(expanded != saved.nodes.end() && !(*expanded)->info->properties->enabled &&
                    (*expanded)->components.front()->payload.AsTransformData()->position.x() == 0 &&
                    (*expanded)->components.front()->payload.AsTransformData()->scale.x() == 1 &&
                    (*expanded)->components.front()->payload.AsTransformData()->rotation.y() ==
                        60 &&
                    (*expanded)->components.back()->payload.AsLightData()->intensity == 5 &&
                    !(*expanded)->components.back()->flags->visible,
                "only explicit fields survive source updates", error))
    {
        return false;
    }
    auto create = identity;
    create.update({{"nodeId", std::to_string(root.id())}, {"path", "copy.prefab"}});
    schema::PrefabDataT copied;
    if (!expect(
            execute("create_prefab", create) &&
                editor::PrefabImporter::read(files.root / "assets/copy.prefab", copied, error) &&
                copied.nodes.size() == 2 && test_support::prefab_instances(*scene).size() == 1,
            "subtree export creates a distinct template without replacing source link", error))
    {
        return false;
    }
    auto root_target = identity;
    root_target["nodeId"] = std::to_string(root.id());
    if (!expect(execute("unlink_prefab", root_target) &&
                    test_support::prefab_instances(*scene).empty() && execute("undo", identity) &&
                    test_support::prefab_instances(*scene).size() == 1,
                "unlink is independently undoable", error))
    {
        return false;
    }
    if (!files.add("copy.prefab", copied) ||
        !(files.publish(resources) && services.prepare_builtins()) ||
        !execute("unlink_prefab", root_target))
    {
        return false;
    }
    if (!child.set_enabled(true))
    {
        return false;
    }
    auto link = root_target;
    link["path"] = "copy.prefab";
    if (!expect(execute("link_prefab", link) &&
                    test_support::prefab_instances(*scene).size() == 1 &&
                    execute("get_prefab_instance", target()) && result["overrides"].size() == 1 &&
                    result["overrides"][0]["fields"] == Json::array({"enabled"}) &&
                    execute("undo", identity) && test_support::prefab_instances(*scene).empty(),
                "explicit link preserves changed values as overrides", error))
    {
        return false;
    }
    if (!child.set_enabled(false))
    {
        return false;
    }
    if (!expect(execute("undo", identity) && test_support::prefab_instances(*scene).size() == 1,
                "undo unlink restores the previous source association", error))
    {
        return false;
    }
    if (!child.set_transient(true))
    {
        return false;
    }
    if (!expect(!execute("save_scene", save), "partial transient prefab cannot silently resurrect",
                error))
    {
        return false;
    }
    if (!child.set_transient(false))
    {
        return false;
    }
    if (!root.set_transient(true))
    {
        return false;
    }
    save["path"] = "transient.scene";
    schema::SceneDataT transient;
    if (!expect(execute("save_scene", save) &&
                    editor::SceneImporter::read(files.root / "assets/transient.scene", transient,
                                                error) &&
                    transient.nodes.empty() && transient.prefab_instances.empty(),
                "whole transient prefab is omitted", error))
    {
        return false;
    }
    if (!root.set_transient(false))
    {
        return false;
    }
    if (!expect(scene->destroy_node(child.id()) && !execute("save_scene", save),
                "implicit deletion is rejected before source members can resurrect", error))
    {
        return false;
    }
    if (!files.add("compiled.scene", saved) ||
        !(files.publish(resources) && services.prepare_builtins()))
    {
        return false;
    }
    if (!services.load("compiled.scene"))
    {
        return false;
    }
    auto* loaded = world.load_scene("compiled.scene");
    return expect(loaded && test_support::prefab_instances(*loaded).size() == 1 &&
                      test_support::scene_nodes(*loaded).size() == 2,
                  "compiled saved source loads with association and selective overrides", error);
}
} // namespace

int main()
{
    return run() ? 0 : 1;
}
