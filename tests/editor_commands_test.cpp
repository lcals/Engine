#include "editor/commands/editor_command_channel.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/editor_edit.hpp"
#include "editor/commands/scene_commands.hpp"
#include "editor/render/render_commands.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/post_process_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

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
    return {{"sceneId", std::to_string(node.scene().id())}, {"nodeId", std::to_string(node.id())}};
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
    if (!module.register_commands(commands))
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
    if (!expect(execute("set_render_settings", {{"ambientIntensity", .3}}) &&
                    renderer.settings().ambient_intensity == .3F &&
                    commands.mutation_revision() == initial_revision,
                "idle defaults do not mutate or create a scene"))
    {
        return false;
    }
    auto* scene = world.create_scene("Persistent scene settings");
    auto* camera_node = scene == nullptr ? nullptr : scene->create_node("Camera");
    auto* camera = camera_node == nullptr ? nullptr : camera_node->add_component<engine::Camera>();
    auto* first_node = scene == nullptr ? nullptr : scene->create_node("Fill light");
    auto* second_node = scene == nullptr ? nullptr : scene->create_node("Shadow light");
    auto* first = first_node == nullptr ? nullptr : first_node->add_component<engine::Light>();
    auto* second = second_node == nullptr ? nullptr : second_node->add_component<engine::Light>();
    if (camera == nullptr || first == nullptr || second == nullptr ||
        !scene->set_active_camera(camera))
    {
        return false;
    }
    auto shadow = second->settings();
    shadow.cast_shadows = true;
    shadow.shadow_resolution = 1024;
    shadow.shadow_bias = .004F;
    if (!second->set_settings(shadow))
    {
        return false;
    }
    const auto count = scene->nodes().size();
    const Json identity = {{"sceneId", std::to_string(scene->id())}};
    if (!expect(execute("set_render_settings", {{"ambientIntensity", .7},
                                                {"exposure", 1.3},
                                                {"directionalShadows", false},
                                                {"iblEnabled", true},
                                                {"iblIntensity", .5}}) &&
                    scene->nodes().size() == count + 1 &&
                    camera_node->get_component<engine::PostProcessComponent>() != nullptr &&
                    renderer.settings().ambient_intensity == .7F &&
                    renderer.settings().ibl_enabled && renderer.settings().ibl_intensity == .5F &&
                    renderer.settings().exposure == 1.3F && !second->settings().cast_shadows &&
                    !first->settings().cast_shadows &&
                    result["shadowResolution"] == first->settings().shadow_resolution &&
                    commands.mutation_revision() > initial_revision,
                "scene edits create components, mark dirty and target the active shadow caster"))
    {
        std::fprintf(stderr, "scene render settings: %s\n", error.c_str());
        return false;
    }
    engine::schema::SceneDataT saved;
    if (!expect(scene->build_data(saved) && saved.nodes.size() == count + 1,
                "scene snapshot contains the environment node") ||
        !expect(execute("undo", identity) && scene->nodes().size() == count &&
                    camera_node->get_component<engine::PostProcessComponent>() == nullptr &&
                    second->settings().cast_shadows &&
                    second->settings().shadow_resolution == 1024 &&
                    second->settings().shadow_bias == .004F && !first->settings().cast_shadows,
                "undo removes created components and restores the exact second light") ||
        !expect(execute("redo", identity) && scene->nodes().size() == count + 1 &&
                    renderer.settings().exposure == 1.3F && !second->settings().cast_shadows,
                "redo recreates the environment with a new node identity") ||
        !expect(execute("undo", identity), "undo before checking rejected settings") ||
        !expect(!execute("set_render_settings", {{"ambientIntensity", .8}, {"exposure", 0}}) &&
                    scene->nodes().size() == count &&
                    camera_node->get_component<engine::PostProcessComponent>() == nullptr &&
                    second->settings().cast_shadows && renderer.settings().ambient_intensity == 0,
                "invalid combined edits leave no partial nodes or components"))
    {
        return false;
    }
    return true;
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
    auto* node = scene ? scene->create_node("Object") : nullptr;
    auto* camera = node ? node->add_component<engine::Camera>() : nullptr;
    auto* light = node ? node->add_component<engine::Light>() : nullptr;
    auto* mesh = node ? node->add_component<engine::MeshRenderer>() : nullptr;
    if (!camera || !light || !mesh || !mesh->set_material_count(2))
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
    auto args = target(*node);
    args.update({{"projection", "orthographic"}, {"orthographicSize", 3}});
    if (!expect(execute("set_camera", args) &&
                    camera->settings().projection == engine::schema::Projection::orthographic,
                "camera projection editable") ||
        !expect(execute("undo", identity) &&
                    camera->settings().projection == engine::schema::Projection::perspective,
                "camera undo restores projection"))
    {
        return false;
    }
    args = target(*node);
    args.update({{"kind", "spot"}, {"range", 7}, {"innerAngle", 15}, {"outerAngle", 25}});
    if (!expect(execute("set_light", args) && light->settings().range == 7 &&
                    light->settings().kind == engine::schema::LightKind::spot,
                "spotlight editing") ||
        !expect(execute("undo", identity) &&
                    light->settings().kind == engine::schema::LightKind::directional,
                "spotlight undo"))
    {
        return false;
    }
    args = target(*node);
    args.update(
        {{"slot", 1}, {"name", "baseColorTexture"}, {"path", "builtin/textures/white-linear"}});
    const auto original = mesh->material(1)->texture("baseColorTexture");
    const auto untouched = mesh->material().texture("baseColorTexture");
    if (!expect(execute("set_material_texture", args) &&
                    mesh->material(1)->texture("baseColorTexture") &&
                    mesh->material(1)->texture("baseColorTexture")->info().path ==
                        "builtin/textures/white-linear" &&
                    !same_texture(mesh->material(1)->texture("baseColorTexture"), original) &&
                    mesh->material().texture("baseColorTexture") == untouched,
                "texture slot selection") ||
        !expect(execute("undo", identity) &&
                    same_texture(mesh->material(1)->texture("baseColorTexture"), original) &&
                    mesh->material().texture("baseColorTexture") == untouched,
                "texture undo restores resource identity and data") ||
        !expect(execute("redo", identity), "texture redo"))
    {
        return false;
    }
    args["path"] = "project/missing.png";
    const auto previous = mesh->material(1)->texture("baseColorTexture");
    if (!expect(!execute("set_material_texture", args) &&
                    mesh->material(1)->texture("baseColorTexture") == previous,
                "failed texture assignment is atomic"))
    {
        return false;
    }
    args = target(*node);
    args.update({{"slot", 1}, {"name", "roughness"}, {"type", "float"}, {"value", 0.8}});
    float value = 0;
    if (!expect(execute("set_material_parameter", args) &&
                    mesh->material(1)->get_float("roughness", value) && value == 0.8F,
                "numeric material slot editing"))
    {
        return false;
    }
    auto bad_camera = target(*node);
    bad_camera["nearClip"] = -1;
    Json batch = {
        {"commands", Json::array({{{"command", "set_material_texture"},
                                   {"arguments",
                                    [&]()
                                    {
                                        auto t = target(*node);
                                        t.update({{"slot", 1},
                                                  {"name", "baseColorTexture"},
                                                  {"path", "builtin/textures/white-srgb"}});
                                        return t;
                                    }()}},
                                  {{"command", "set_camera"}, {"arguments", bad_camera}}})}};
    if (!expect(!execute("batch", batch) &&
                    same_texture(mesh->material(1)->texture("baseColorTexture"), previous) &&
                    mesh->material().texture("baseColorTexture") == untouched,
                "batch rolls texture back when camera fails"))
    {
        return false;
    }
    return expect(execute("get_scene", Json::object()) &&
                      result["scene"]["nodes"][0]["components"][3]["materials"].size() == 2,
                  "snapshot exposes all material slots");
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
    auto* cube = scene != nullptr ? scene->create_node("Cube") : nullptr;
    auto* lamp = scene != nullptr ? scene->create_node("Light") : nullptr;
    auto* mesh = cube != nullptr ? cube->add_component<engine::MeshRenderer>() : nullptr;
    auto* light = lamp != nullptr ? lamp->add_component<engine::Light>() : nullptr;
    if (!expect(cube != nullptr && lamp != nullptr && mesh != nullptr && light != nullptr,
                "create runtime nodes and components"))
    {
        return false;
    }
    auto* transform = cube->get_component<engine::Transform>();
    const Json identity = {{"sceneId", std::to_string(scene->id())}};
    auto position = target(*cube);
    position["position"] = {1, 2, 3};
    if (!expect(execute("get_scene", Json::object()) &&
                    result["scene"]["nodes"][0]["nodeId"] == std::to_string(cube->id()) &&
                    result["scene"]["nodes"][0]["components"].size() == 2,
                "query exposes complete string identities and component data") ||
        !expect(execute("set_transform", position) && transform->settings().position.x() == 1 &&
                    commands.undo_count() == 1,
                "apply transform once") ||
        !expect(execute("undo", identity) && transform->settings().position.x() == 0 &&
                    cube->get_component<engine::Transform>() == transform &&
                    scene->find_node(cube->id()) == cube,
                "undo preserves node and component identities") ||
        !expect(execute("redo", identity) && transform->settings().position.x() == 1,
                "redo reapplies the same edit"))
    {
        return false;
    }
    auto changed = transform->settings();
    changed.rotation.mutate_y(50);
    if (!expect(transform->set_settings(changed) && execute("undo", identity) &&
                    transform->settings().rotation.y() == 50,
                "undo preserves unrelated script changes") ||
        !expect(execute("redo", identity), "redo after unrelated field change"))
    {
        return false;
    }
    changed = transform->settings();
    changed.position.mutate_x(99);
    if (!expect(transform->set_settings(changed) && !execute("undo", identity) &&
                    transform->settings().position.x() == 99 && commands.undo_count() == 1,
                "stale undo cannot overwrite a script change"))
    {
        return false;
    }
    commands.clear_history();
    auto invalid = position;
    invalid["scale"] = {1, 0, 1};
    if (!expect(!execute("set_transform", invalid) && transform->settings().position.x() == 99 &&
                    commands.undo_count() == 0,
                "invalid setter leaves all fields unchanged"))
    {
        return false;
    }
    invalid = position;
    invalid["nodeId"] = cube->id();
    if (!expect(!execute("set_transform", invalid), "numeric JSON node IDs are rejected"))
    {
        return false;
    }
    invalid["nodeId"] = "18446744073709551616";
    if (!expect(!execute("set_transform", invalid), "uint64 overflow is rejected"))
    {
        return false;
    }
    auto bad_light = target(*cube);
    bad_light["intensity"] = 2;
    const Json failed_batch = {{"commands",
                                {{{"command", "set_transform"}, {"arguments", position}},
                                 {{"command", "set_light"}, {"arguments", bad_light}}}}};
    if (!expect(!execute("batch", failed_batch) && transform->settings().position.x() == 99 &&
                    commands.undo_count() == 0,
                "failed batch rolls back earlier edits without creating history"))
    {
        return false;
    }
    auto lighting = target(*lamp);
    lighting["intensity"] = 9;
    const Json batch = {{"commands",
                         {{{"command", "set_transform"}, {"arguments", position}},
                          {{"command", "set_light"}, {"arguments", lighting}}}}};
    const float old_light = light->settings().intensity;
    if (!expect(execute("batch", batch) && commands.undo_count() == 1 &&
                    light->settings().intensity == 9,
                "successful batch is one history item"))
    {
        return false;
    }
    changed = transform->settings();
    changed.position.mutate_x(77);
    if (!expect(transform->set_settings(changed) && !execute("undo", identity) &&
                    transform->settings().position.x() == 77 && light->settings().intensity == 9,
                "stale batch undo restores any members it already reverted"))
    {
        return false;
    }
    changed.position = {1, 2, 3};
    if (!expect(transform->set_settings(changed) && execute("undo", identity) &&
                    transform->settings().position.x() == 99 &&
                    light->settings().intensity == old_light && commands.redo_count() == 1,
                "one undo restores the whole batch"))
    {
        return false;
    }
    auto external_light = light->settings();
    external_light.intensity = 14;
    if (!expect(light->set_settings(external_light) && !execute("redo", identity) &&
                    transform->settings().position.x() == 99 && light->settings().intensity == 14,
                "stale batch redo rolls back its earlier applied members"))
    {
        return false;
    }
    external_light.intensity = old_light;
    if (!expect(light->set_settings(external_light) && execute("redo", identity) &&
                    transform->settings().position.x() == 1 && light->settings().intensity == 9,
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
                    transform->settings().position.x() == 1 && execute("redo", identity) &&
                    transform->settings().position.x() == 4,
                "batch records sequential changes to the same property"))
    {
        return false;
    }
    auto parameter = target(*cube);
    parameter.update({{"name", "roughness"}, {"type", "float"}, {"value", 0.7}});
    float roughness = 0;
    if (!expect(execute("set_material_parameter", parameter) &&
                    mesh->material().get_float("roughness", roughness) &&
                    std::abs(roughness - 0.7F) < 0.0001F,
                "material command uses the existing native binding") ||
        !expect(execute("undo", identity) && mesh->material().get_float("roughness", roughness) &&
                    std::abs(roughness - 0.4F) < 0.0001F,
                "material undo restores only the named parameter"))
    {
        return false;
    }
    parameter["value"] = 2;
    if (!expect(!execute("set_material_parameter", parameter) &&
                    mesh->material().get_float("roughness", roughness) && roughness == 0.4F,
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
                    read(originals / "first.json")["success"] == true,
                "channel persists the first receipt") ||
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
        !expect(write(input, request("third")) && !channel.poll() && count == 3,
                "receipt write failure retains the completed effect") ||
        !expect(write(input, request("fourth")) && !channel.poll() && count == 3,
                "pending receipt blocks subsequent command execution"))
    {
        return false;
    }
    fs::remove(latest, code);
    if (code ||
        !expect(channel.poll() && count == 4 &&
                    read(originals / "third.json")["result"]["value"] == 3,
                "retry publishes old result before the next request") ||
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
    if (!command_tests() || !render_command_tests() || !scene_render_settings_tests())
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
