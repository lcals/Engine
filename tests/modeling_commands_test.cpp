#include "editor/commands/editor_commands.hpp"
#include "plugins/editor/modeling/modeling_commands.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#ifdef ENGINE_TEST_MODELING_MCP
#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#include "plugins/editor/mcp/mcp_system.hpp"
#include "plugins/editor/modeling/modeling_mcp_tools.hpp"
#endif

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace
{
using Json = nlohmann::json;
namespace fs = std::filesystem;

bool expect(bool value, const char* message, const std::string& error)
{
    if (!value)
    {
        std::fprintf(stderr, "modeling commands: %s: %s\n", message, error.c_str());
    }
    return value;
}

#ifdef ENGINE_TEST_MODELING_MCP
bool registry_test()
{
    engine::EditorMcpTools editor({}, {});
    engine::ModelingMcpTools module(editor);
    engine::McpSystem system;
    Json reply;
    if (!expect(module.register_tools(system), "register all modeling schemas", {}) ||
        !expect(!module.register_tools(system), "duplicate modeling registration rejected", {}) ||
        !expect(system.list_tools()["tools"].size() == 11, "all tools are discoverable", {}) ||
        !expect(system.initialize({}), "initialize registry", {}))
    {
        return false;
    }
    for (const char* operation : {"transform",
                                  "extrude",
                                  "extrude_edges",
                                  "inset",
                                  "erase_faces",
                                  "reverse",
                                  "triangulate",
                                  "subdivide",
                                  "weld",
                                  "split_vertices",
                                  "merge_vertices",
                                  "detach",
                                  "duplicate_faces",
                                  "bridge",
                                  "fill_hole",
                                  "dissolve_edge",
                                  "split_edge",
                                  "loop_cut",
                                  "connect_vertices",
                                  "mirror",
                                  "mirror_mode",
                                  "clip",
                                  "bevel",
                                  "boolean",
                                  "project_uv",
                                  "transform_uv",
                                  "unwrap_uv",
                                  "unwrap_face_uv",
                                  "hotspot_uv",
                                  "smoothing",
                                  "auto_smooth",
                                  "material",
                                  "color"})
    {
        const Json arguments = {{"sceneId", "1"}, {"modelId", "1"}, {"operation", operation}};
        if (!expect(system.call_tool("modeling_edit", arguments, reply) ==
                        engine::McpCallStatus::failed,
                    "every modeling operation passes schema and reaches the absent desktop target",
                    operation))
        {
            return false;
        }
    }
    for (const char* shape :
         {"cube", "plane", "cylinder", "cone", "sphere", "torus", "prism", "stairs", "arch", "pipe",
          "door", "curved_stairs", "sprite", "icosphere", "polygon", "sweep", "lathe"})
    {
        if (!expect(system.call_tool("modeling_create", {{"sceneId", "1"}, {"shape", shape}},
                                     reply) == engine::McpCallStatus::failed,
                    "every shape passes tool schema", shape))
        {
            return false;
        }
    }
    for (const char* mode : {"replace", "grow", "shrink", "connected", "edge_ring", "edge_loop"})
    {
        if (!expect(system.call_tool("modeling_select",
                                     {{"sceneId", "1"}, {"modelId", "1"}, {"mode", mode}},
                                     reply) == engine::McpCallStatus::failed,
                    "every selection mode passes tool schema", mode))
        {
            return false;
        }
    }
    return expect(system.call_tool("modeling_edit", {{"sceneId", "1"}, {"modelId", 1}}, reply) ==
                      engine::McpCallStatus::invalid_arguments,
                  "invalid schema arguments rejected before editor transport", {}) &&
           expect(system.call_tool("modeling_create", {{"sceneId", "1"}, {"shape", "unknown"}},
                                   reply) == engine::McpCallStatus::invalid_arguments,
                  "unknown shape rejected by tool schema", {});
}
#endif

bool run(const fs::path& project)
{
    engine::ResourceSystem resources;
    engine::WorldSystem world;
    if (!resources.initialize({}) || !world.initialize({}))
    {
        return false;
    }
    auto* scene = world.create_scene("Modeling");
    if (!scene)
    {
        return false;
    }
    engine::ModelingCommands modeling(world, project);
    engine::EditorCommands commands([&]() { return world.scene() ? world.scene()->id() : 0; });
    if (!modeling.register_commands(commands))
    {
        return false;
    }
    Json result;
    std::string error;
    const Json identity = {{"sceneId", std::to_string(scene->id())}};
    const auto execute = [&](std::string_view command, const Json& arguments)
    { return commands.execute(command, arguments, result, error); };
    auto create = identity;
    create.update({{"shape", "cube"}, {"name", "Editable cube"}});
    if (!expect(execute("modeling_create", create) && scene->nodes().size() == 1,
                "create Node and renderer", error))
    {
        return false;
    }
    auto target = identity;
    target["modelId"] = result["modelId"];
    if (!expect(execute("modeling_get", target) && result["model"]["faceCount"] == 6 &&
                    result["model"]["vertexCount"] == 8,
                "query stable topology", error))
    {
        return false;
    }
    const auto original = result;
    const auto old_node = result["model"]["nodeId"];
    auto edit = target;
    edit.update({{"operation", "extrude"},
                 {"selection", {{"faces", Json::array({result["faces"][0]["id"]})}}},
                 {"parameters", {{"distance", 0.25}}}});
    if (!expect(execute("modeling_edit", edit), "extrude through pure modeling core", error) ||
        !expect(execute("undo", identity), "undo topology edit", error) ||
        !expect(execute("modeling_get", target) && result["faces"] == original["faces"] &&
                    result["model"]["nodeId"] == old_node,
                "geometry undo preserves node and face identity", error))
    {
        return false;
    }
    auto invalid = edit;
    invalid["parameters"]["distance"] = "not a number";
    const auto history = commands.undo_count();
    if (!expect(!execute("modeling_edit", invalid) && commands.undo_count() == history,
                "invalid operation is atomic", error))
    {
        return false;
    }
    Json batch = {
        {"commands", Json::array({{{"command", "modeling_edit"}, {"arguments", edit}},
                                  {{"command", "modeling_edit"}, {"arguments", invalid}}})}};
    if (!expect(!execute("batch", batch), "batch rejects invalid second operation", error) ||
        !expect(execute("modeling_get", target) && result["faces"] == original["faces"],
                "failed batch restores original source", error))
    {
        return false;
    }
    auto save = target;
    save["path"] = "models/cube.mesh";
    if (!expect(execute("modeling_save", save) &&
                    fs::is_regular_file(project / "assets/models/cube.mesh"),
                "save source with schema data", error))
    {
        return false;
    }
    auto load = identity;
    load["path"] = "models/cube.mesh";
    if (!expect(execute("modeling_load", load) && scene->nodes().size() == 2, "load saved model",
                error) ||
        !expect(execute("undo", identity) && scene->nodes().size() == 1, "undo loaded model",
                error))
    {
        return false;
    }
    save["path"] = "../escape.mesh";
    if (!expect(!execute("modeling_save", save) && !fs::exists(project / "escape.mesh"),
                "reject path traversal", error))
    {
        return false;
    }
    if (!expect(execute("modeling_delete", target) && scene->nodes().empty(),
                "delete modeling node", error) ||
        !expect(execute("undo", identity) && scene->nodes().size() == 1,
                "undo deletion restores model", error) ||
        !expect(execute("modeling_get", target) && result["model"]["nodeId"] != old_node &&
                    result["faces"] == original["faces"],
                "restored model keeps topology identity and receives fresh node identity", error))
    {
        return false;
    }
    auto stale = target;
    stale["nodeId"] = old_node;
    if (!expect(!execute("modeling_get", stale), "old node ID is rejected", error))
    {
        return false;
    }
    auto selection = target;
    selection["selection"] = {{"all", true}};
    if (!expect(execute("modeling_select", selection), "persistent selection", error) ||
        !expect(execute("undo", identity), "selection uses shared undo history", error))
    {
        return false;
    }
    auto clear_selection = target;
    clear_selection["selection"] = {{"all", false},
                                    {"vertices", Json::array()},
                                    {"faces", Json::array()},
                                    {"edges", Json::array()}};
    if (!expect(execute("modeling_select", selection) &&
                    execute("modeling_select", clear_selection),
                "replace selection explicitly accepts empty lists", error) ||
        !expect(execute("modeling_get", target) &&
                    result["model"]["selection"] == clear_selection["selection"],
                "selection is cleared without changing source topology", error))
    {
        return false;
    }
    auto empty_face_edit = target;
    empty_face_edit["operation"] = "extrude";
    if (!expect(!execute("modeling_edit", empty_face_edit),
                "cleared selection still rejects a face operation", error) ||
        !expect(execute("undo", identity) && execute("modeling_get", target) &&
                    result["model"]["selection"]["all"] == true,
                "undo restores prior selection after clear", error) ||
        !expect(execute("undo", identity), "restore initial selection history", error))
    {
        return false;
    }
    const auto first_edge =
        Json::array({original["faces"][0]["vertices"][0], original["faces"][0]["vertices"][1]});
    auto duplicate_edges = target;
    duplicate_edges["selection"] = {
        {"edges", Json::array({first_edge, Json::array({first_edge[1], first_edge[0]})})}};
    if (!expect(!execute("modeling_select", duplicate_edges),
                "selection rejects reversed duplicate edges", error))
    {
        return false;
    }
    if (!expect(execute("modeling_help", {{"operation", "unwrap_uv"}}),
                "query executable unwrap example", error))
    {
        return false;
    }
    auto unwrap_example = result["contract"]["example"];
    unwrap_example.update(target);
    if (!expect(unwrap_example["parameters"]["seams"].is_array() &&
                    unwrap_example["parameters"]["seams"].empty() &&
                    execute("modeling_edit", unwrap_example),
                "published unwrap example executes with empty explicit seams", error) ||
        !expect(execute("undo", identity) && execute("modeling_get", target) &&
                    result["faces"] == original["faces"],
                "undo unwrap example restores source UVs", error))
    {
        return false;
    }
    auto wrong_uv_selection = target;
    wrong_uv_selection.update(
        {{"operation", "project_uv"},
         {"selection", {{"vertices", Json::array({original["vertices"][0]["id"]})}}}});
    if (!expect(!execute("modeling_edit", wrong_uv_selection),
                "vertex-only selection cannot implicitly unwrap all faces", error) ||
        !expect(execute("modeling_get", target) && result["faces"] == original["faces"],
                "invalid face selection leaves UVs unchanged", error))
    {
        return false;
    }
    auto material = target;
    material.update(
        {{"operation", "material"}, {"selection", {{"all", true}}}, {"parameters", {{"slot", 1}}}});
    if (!expect(execute("modeling_edit", material) &&
                    scene->nodes()[0]->get_component<engine::MeshRenderer>()->material_count() == 2,
                "face material assignment grows renderer slots", error) ||
        !expect(execute("undo", identity) &&
                    scene->nodes()[0]->get_component<engine::MeshRenderer>()->material_count() == 1,
                "material assignment undo restores slot count", error))
    {
        return false;
    }
    auto* parent = scene->create_node("Model parent");
    auto* source_node = scene->nodes()[0].get();
    if (!parent)
    {
        return false;
    }
    auto parent_settings = parent->get_component<engine::Transform>()->settings();
    parent_settings.position = {10, 0, 0};
    if (!expect(parent->get_component<engine::Transform>()->set_settings(parent_settings) &&
                    source_node->set_parent(parent) && execute("modeling_duplicate", target),
                "duplicate parented model", error))
    {
        return false;
    }
    auto parented_copy = identity;
    parented_copy["modelId"] = result["modelId"];
    if (!expect(execute("modeling_get", parented_copy) &&
                    result["vertices"][0]["position"][0].get<double>() ==
                        original["vertices"][0]["position"][0].get<double>() + 10,
                "parented copy bakes world transform instead of jumping to local position",
                error) ||
        !expect(execute("undo", identity) && source_node->set_parent(nullptr) &&
                    scene->destroy_node(parent->id()),
                "restore parented-copy test scene", error))
    {
        return false;
    }
    if (!expect(execute("modeling_duplicate", target), "duplicate model", error))
    {
        return false;
    }
    const auto duplicate_id = result["modelId"];
    auto combination = identity;
    combination["modelIds"] = {target["modelId"], duplicate_id};
    if (!expect(execute("modeling_combine", combination) && scene->nodes().size() == 3,
                "combine creates a model without destroying inputs", error))
    {
        return false;
    }
    auto combined_target = identity;
    combined_target["modelId"] = result["modelId"];
    if (!expect(execute("modeling_get", combined_target) && result["model"]["faceCount"] == 12,
                "combine uses core topology append", error) ||
        !expect(execute("undo", identity) && scene->nodes().size() == 2, "undo combined object",
                error) ||
        !expect(execute("undo", identity) && scene->nodes().size() == 1, "undo duplicate object",
                error))
    {
        return false;
    }
    auto separation = target;
    separation["selection"] = {{"faces", Json::array({original["faces"][0]["id"]})}};
    if (!expect(execute("modeling_separate", separation) && scene->nodes().size() == 2,
                "separate selected faces", error) ||
        !expect(execute("modeling_get", target) && result["model"]["faceCount"] == 5,
                "separate removes selected face from source", error) ||
        !expect(execute("undo", identity) && scene->nodes().size() == 1,
                "separate undo restores both objects atomically", error))
    {
        return false;
    }
    auto mirror = target;
    mirror.update(
        {{"operation", "mirror_mode"},
         {"parameters",
          {{"action", "enable"}, {"normal", {1, 0, 0}}, {"offset", 2}, {"weld", false}}}});
    if (!expect(execute("modeling_edit", mirror), "enable continuous mirror", error) ||
        !expect(execute("modeling_get", target) && result["model"]["faceCount"] == 6 &&
                    result["model"]["triangleCount"] == 24,
                "mirror renders derived data while retaining source half", error))
    {
        return false;
    }
    const auto mirrored = result;
    auto crossing = target;
    crossing.update(
        {{"operation", "transform"},
         {"selection", {{"all", true}}},
         {"parameters", {{"matrix", {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 2, 0, 0, 1}}}}});
    if (!expect(!execute("modeling_edit", crossing), "reject source crossing active mirror plane",
                error) ||
        !expect(execute("modeling_get", target) && result["vertices"] == mirrored["vertices"] &&
                    result["model"]["mirror"]["enabled"] == true,
                "rejected mirror edit keeps source and render mode", error))
    {
        return false;
    }
    auto finish = target;
    finish.update({{"operation", "mirror_mode"}, {"parameters", {{"action", "finish"}}}});
    if (!expect(execute("modeling_edit", finish), "finish mirror mode", error) ||
        !expect(execute("modeling_get", target) && result["model"]["faceCount"] == 12 &&
                    result["model"]["mirror"]["enabled"] == false,
                "finish mirror bakes source topology", error) ||
        !expect(execute("undo", identity), "undo mirror finish", error) ||
        !expect(execute("modeling_get", target) && result["model"]["faceCount"] == 6 &&
                    result["model"]["mirror"]["enabled"] == true,
                "undo restores source half and mirror mode", error))
    {
        return false;
    }
    finish["parameters"]["action"] = "cancel";
    if (!expect(execute("modeling_edit", finish), "cancel continuous mirror", error))
    {
        return false;
    }
    if (!expect(execute("modeling_help", {{"operation", "boolean"}}) &&
                    result["contract"]["parameters"]["properties"].contains("otherModelId"),
                "MCP clients can discover operation parameters", error))
    {
        return false;
    }
    world.destroy_scene();
    if (!expect(!execute("modeling_get", target) && commands.undo_count() == 0,
                "scene shutdown invalidates models and history", error))
    {
        return false;
    }
    return true;
}
} // namespace

int main()
{
    std::error_code error;
    const auto base = fs::temp_directory_path(error);
    if (error)
    {
        return 1;
    }
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto project = base / ("engine-modeling-" + std::to_string(unique));
    fs::create_directories(project / "assets", error);
    bool success = !error && run(project);
#ifdef ENGINE_TEST_MODELING_MCP
    success = registry_test() && success;
#endif
    fs::remove_all(project, error);
    return success ? 0 : 1;
}
