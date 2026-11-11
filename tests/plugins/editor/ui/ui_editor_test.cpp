#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/scene_commands.hpp"
#include "editor/editor_environment.hpp"
#include "editor/project/project_system.hpp"
#include "editor/scene/scene_preview.hpp"
#include "plugins/editor/asset_import/font_importer.hpp"
#include "plugins/editor/asset_import/ui_importer.hpp"
#include "plugins/editor/ui/ui_editor_commands.hpp"
#include "plugins/editor/ui/ui_editor_state.hpp"
#include "resource_services.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/font/font_system.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/input/input_system.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/ui_component.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"
#include "runtime/ui/ui_system.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/world/world_system.hpp"
#include "utf8_fixture.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>

namespace
{
using Json = nlohmann::json;
namespace fs = std::filesystem;
bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "UI editor: %s\n", message);
    }
    return value;
}
bool write(const fs::path& path, std::string_view value)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(value.data(), static_cast<std::streamsize>(value.size()));
    stream.close();
    return bool(stream);
}
constexpr std::string_view markup = R"(<rml><head><title>UI editor fixture</title><style>
body { font-family: body; font-size: 18px; margin:0; }
#button {position:absolute;left:20px;top:20px;width:160px;height:40px;}
#field {position:absolute;left:20px;top:80px;width:220px;height:40px;}
#password {position:absolute;left:20px;top:135px;width:220px;height:40px;}
#cover {position:absolute;left:20px;top:20px;width:160px;height:40px;z-index:4;visibility:hidden;}
#scroll {position:absolute;left:280px;top:20px;width:160px;height:100px;overflow:auto;}
#long {height:600px;}
</style></head><body data-input-capture="true">
<button id="button">Press</button><input id="field" type="text" value=""/>
<input id="password" type="password" value="secret"/>
<div id="scroll"><div id="long">Scrollable</div></div>
<button id="cover">Covered</button></body></rml>)";
struct Fixture
{
    fs::path root;
    engine::ThreadSystem threads;
    test_support::ResourceServices services{&threads};
    engine::ResourceSystem& resources = services.core;
    engine::TaskSystem tasks{threads};
    engine::editor::AssetPipelineSystem pipeline{tasks};
    engine::WindowSystem window;
    engine::WorldSystem world{&services.loader};
    engine::FontSystem fonts{&services.loader};
    engine::UiSystem ui{window, world, fonts};
    engine::RenderSystem renderer{window, &world, &fonts, &ui};
    engine::InputSystem input{window};
    engine::GameplaySession gameplay{resources, world, renderer, nullptr};
    engine::editor::ProjectSystem project;
    engine::editor::EditorEnvironment environment{project};
    engine::editor::ScenePreview preview;
    engine::plugins::ui::UiEditorCommands module;
    engine::editor::SceneCommands scene_commands{world};
    engine::editor::EditorCommands commands;
    explicit Fixture(fs::path p)
        : root(std::move(p)), project(engine::PathUtils::to_utf8(root)),
          preview(environment, gameplay, world, renderer, input, root,
                  [](std::string&) { return true; }),
          module(world, ui, window, preview, tasks, root),
          commands([this] { return world.scene() ? world.scene()->id() : 0; })
    {
    }
    ~Fixture()
    {
        module.cancel_pending_input();
        ui.shutdown();
        fonts.shutdown();
        renderer.shutdown();
        world.shutdown();
        window.shutdown();
        services.shutdown();
        pipeline.shutdown();
        tasks.shutdown();
        threads.shutdown();
    }
    bool initialize(const fs::path& source)
    {
        std::error_code error;
        for (const auto* directory : {"assets/fonts", "assets/ui", "cache", "settings"})
        {
            fs::create_directories(root / directory, error);
        }
        if (error)
        {
            return false;
        }
        if (!write(root / "settings/editor-import.toml", "[common]\nimport_threads = 8\n"))
        {
            return false;
        }
        for (const auto* file : {"lato-regular.ttf", "lato-regular.ttf.meta"})
        {
            fs::copy_file(source / "assets/fonts" / file, root / "assets/fonts" / file,
                          fs::copy_options::overwrite_existing, error);
            if (error)
            {
                return false;
            }
        }
        if (!write(root / "assets/ui/fixture.rml", markup) ||
            !write(root / "assets/ui/fixture.rml.meta",
                   R"(guid="60000000-0000-4000-8000-000000000001"
importer="ui"
meta_version=1
[ui]
textures=[]
[[ui.fonts]]
family="body"
asset="fonts/lato-regular.ttf"
shader="builtin://shaders/font"
)"))
        {
            return false;
        }
        engine::EngineConfig config;
        config.width = 640;
        config.height = 480;
        config.window_title = "Engine UI editor integration";
        std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> font =
            std::make_unique<engine::plugins::asset_import::FontImporter>();
        std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> document =
            std::make_unique<engine::plugins::asset_import::UiImporter>();
        return threads.initialize(config) && tasks.initialize(config) &&
               pipeline.initialize(config) && pipeline.add_importer(font) &&
               pipeline.add_importer(document) &&
               expect(pipeline.build_assets(root), pipeline.last_error().c_str()) &&
               services.initialize(config) &&
               services.publish_and_load(pipeline.catalog(), {{"content", root / "cache"}}) &&
               window.initialize(config) && world.initialize(config) && fonts.initialize(config) &&
               ui.initialize(config) && renderer.initialize(config) &&
               scene_commands.register_commands(commands) &&
               module.register_commands(commands, scene_commands);
    }
    bool call(std::string_view name, const Json& args, Json& result)
    {
        std::string error;
        const bool success = commands.execute(name, args, result, error);
        if (!success)
        {
            std::fprintf(stderr, "UI command %.*s: %s\n", int(name.size()), name.data(),
                         error.c_str());
        }
        return success;
    }
    bool job(std::string_view command, const Json& args, Json& result)
    {
        Json accepted;
        if (!call(command, args, accepted) || !accepted.contains("job"))
        {
            return false;
        }
        const bool source = command == "get_ui_source" || command == "save_ui_source";
        const auto started = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - started < std::chrono::seconds(10))
        {
            (void)tasks.dispatch_completions(16, .01);
            module.tick(1.0 / 60.0);
            if (!call(source ? "get_ui_source" : "get_ui_input", {{"job", accepted["job"]}},
                      result))
            {
                return false;
            }
            if (result["status"] != "running")
            {
                if (result["status"] == "failed")
                {
                    std::fprintf(stderr, "UI job %.*s: %s\n", int(command.size()), command.data(),
                                 result.dump().c_str());
                }
                return true;
            }
            SDL_Delay(1);
        }
        return false;
    }
};
Json target(engine::UiElementHandle h)
{
    return {{"documentId", std::to_string(h.document_id)},
            {"elementId", std::to_string(h.element_id)}};
}
std::size_t clicks(engine::UiSystem& ui, std::string_view id)
{
    std::size_t count = 0;
    engine::UiEvent event;
    while (ui.poll_event(event))
    {
        if (event.element_id == id && event.type == "click")
        {
            ++count;
        }
    }
    return count;
}
bool test(Fixture& f)
{
    auto* scene = f.world.create_scene("UI editor fixture");
    auto node = scene ? scene->create_node("UI") : engine::Node{};
    auto component = node ? node.add_component<engine::UiComponent>() : engine::UiComponent{};
    if (!component || !component.set_document("ui/fixture.rml") || !f.ui.synchronize())
    {
        return false;
    }
    engine::UiElementHandle button, field, password, cover, scroll;
    if (!f.ui.find_element(node.id(), "button", button) ||
        !f.ui.find_element(node.id(), "field", field) ||
        !f.ui.find_element(node.id(), "password", password) ||
        !f.ui.find_element(node.id(), "cover", cover) ||
        !f.ui.find_element(node.id(), "scroll", scroll))
    {
        return false;
    }
    Json reply;
    if (!expect(f.call("get_ui_tree",
                       {{"nodeId", std::to_string(node.id())},
                        {"documentId", std::to_string(button.document_id)},
                        {"limit", 2}},
                       reply) &&
                    reply["elements"].size() == 2 && reply["truncated"] == true &&
                    reply["nextOffset"] == 2,
                "tree pagination includes no-id DOM nodes") ||
        !expect(f.call("get_ui_element", {{"target", target(password)}}, reply) &&
                    reply["value"] == "******",
                "password inspection masks values"))
    {
        return false;
    }
    if (!expect(f.job("click_ui", {{"target", target(button)}}, reply) &&
                    reply["status"] == "complete" && clicks(f.ui, "button") == 1,
                "click traverses actual RmlUi and creates one business click"))
    {
        return false;
    }
    if (!expect(f.job("click_ui", {{"target", target(button)}, {"clickCount", 2}}, reply) &&
                    reply["status"] == "complete" && clicks(f.ui, "button") == 2,
                "double click uses two real paired button sequences"))
    {
        return false;
    }
    Json events = Json::array({{{"type", "focus"}, {"target", target(field)}},
                               {{"type", "text"},
                                {"target", target(field)},
                                {"text", test_support::utf8_fixture("ui_input")}},
                               {{"type", "key_down"}, {"key", "Shift"}},
                               {{"type", "key_down"}, {"key", "Left"}},
                               {{"type", "key_up"}, {"key", "Left"}},
                               {{"type", "key_up"}, {"key", "Shift"}},
                               {{"type", "text"}, {"text", "!"}}});
    std::string value;
    if (!expect(f.job("ui_input",
                      {{"documentId", std::to_string(field.document_id)}, {"events", events}},
                      reply) &&
                    reply["status"] == "complete" && f.ui.value(field, value) &&
                    value == test_support::utf8_fixture("ui_edited"),
                "Unicode input and Shift selection use native modifier state"))
    {
        return false;
    }
    if (!f.ui.set_visible(cover, true))
    {
        return false;
    }
    if (!expect(f.job("click_ui", {{"target", target(button)}}, reply) &&
                    reply["status"] == "failed" && clicks(f.ui, "button") == 0,
                "occluded targets fail without dispatching business clicks") ||
        !f.ui.set_visible(cover, false))
    {
        return false;
    }
    events =
        Json::array({{{"type", "pointer_down"}, {"target", target(button)}, {"button", "left"}},
                     {{"type", "wait"}, {"milliseconds", 1000}},
                     {{"type", "pointer_up"}, {"target", target(button)}, {"button", "left"}}});
    if (!f.call("ui_input",
                {{"documentId", std::to_string(button.document_id)}, {"events", events}}, reply))
    {
        return false;
    }
    const auto canceled = reply["job"];
    f.module.tick(.01);
    SDL_Event native{};
    native.type = SDL_EVENT_MOUSE_BUTTON_UP;
    f.module.observe_event(native);
    if (!expect(f.call("get_ui_input", {{"job", canceled}}, reply) &&
                    reply["status"] == "canceled" && !f.ui.pointer_active() &&
                    clicks(f.ui, "button") == 0,
                "native input cancels captured presses without activation"))
    {
        return false;
    }
    const auto source_path = "ui/fixture.rml";
    if (!f.job("get_ui_source", {{"path", source_path}}, reply) || reply["status"] != "complete")
    {
        return false;
    }
    const auto before = reply["result"];
    Json save{{"path", source_path},
              {"source", std::string(markup) + "\n"},
              {"expectedSourceHash", before["sourceHash"]},
              {"expectedMetaHash", before["metaHash"]}};
    if (!expect(f.job("save_ui_source", save, reply) && reply["status"] == "complete" &&
                    reply["result"]["guid"] == before["guid"] &&
                    reply["result"]["import"] == "pending",
                "source save preserves GUID and distinguishes import pending") ||
        !expect(f.job("save_ui_source", save, reply) && reply["status"] == "failed",
                "stale source hash rejects overwrites") ||
        !expect(f.job("get_ui_source", {{"path", "../outside.rml"}}, reply) &&
                    reply["status"] == "failed",
                "source path cannot escape assets"))
    {
        return false;
    }
    if (!f.job("get_ui_source", {{"path", source_path}}, reply) || reply["status"] != "complete")
    {
        return false;
    }
    save["expectedSourceHash"] = reply["result"]["sourceHash"];
    save["expectedMetaHash"] = reply["result"]["metaHash"];
    save["source"] = "<rml><body><img src=\"undeclared.png\"/></body></rml>";
    if (!expect(f.job("save_ui_source", save, reply) && reply["status"] == "failed",
                "UiImporter rejects undeclared resources before source publish"))
    {
        return false;
    }
    engine::UiElementHandle child;
    engine::UiElementInfo info;
    if (!expect(f.ui.child_element(button, 0, child) && f.ui.set_text(button, "New label") &&
                    !f.ui.inspect_element(child, info),
                "replaced child cannot resurrect a stale handle"))
    {
        return false;
    }
    const auto old = field;
    if (!component.set_document("ui/fixture.rml"))
    {
        return false;
    }
    // Destroy/recreate the component at the same node: old document handles must stay invalid.
    if (!node.remove_component(engine::UiComponent::component_type))
    {
        return false;
    }
    component = node.add_component<engine::UiComponent>();
    if (!expect(component && component.set_document("ui/fixture.rml") && !f.ui.is_valid(old),
                "component recreation invalidates prior document handles"))
    {
        return false;
    }
    std::string validation_error;
    if (!expect(!engine::plugins::ui::validate_input(
                    Json::array({{{"type", "key_down"}, {"key", "Shift"}}}), old.document_id,
                    validation_error),
                "unpaired remote modifier is rejected before input") ||
        !expect(!engine::plugins::ui::validate_input(
                    Json::array({{{"type", "wait"}, {"milliseconds", 6000}},
                                 {{"type", "wait"}, {"milliseconds", 6000}}}),
                    old.document_id, validation_error),
                "long input waits are rejected before input"))
    {
        return false;
    }
    return true;
}
} // namespace
int main(int argc, char** argv)
{
    if (argc != 3)
    {
        return 2;
    }
    const fs::path root =
        engine::PathUtils::from_utf8(argv[2]) /
        ("ui-editor-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    bool success = false;
    {
        Fixture fixture(root);
        success = fixture.initialize(engine::PathUtils::from_utf8(argv[1])) && test(fixture);
    }
    std::error_code error;
    fs::remove_all(root, error);
    if (success)
    {
        std::puts("UI editor: actual click, Unicode/modifiers, occlusion, native cancellation, "
                  "source conflict and validation passed");
    }
    return success ? 0 : 1;
}
