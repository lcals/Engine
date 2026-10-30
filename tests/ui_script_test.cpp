#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "plugins/editor/font_import/font_importer.hpp"
#include "plugins/editor/ui_import/ui_importer.hpp"
#include "plugins/runtime/script/script_system.hpp"
#include "runtime/app/app.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/font/font_system.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/ui_component.hpp"
#include "runtime/ui/ui_system.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <bgfx/bgfx.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
namespace fs = std::filesystem;
constexpr double delta_time = 1.0 / 60.0;
constexpr std::string_view main_scene = "project/scenes/main.scene";
constexpr std::string_view main_document = "project/ui/main.rml";
constexpr std::string_view ui_guid = "29000000-0000-4000-8000-000000000001";
constexpr std::string_view alternate_guid = "29000000-0000-4000-8000-000000000002";

bool expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::fprintf(stderr, "UI script: %.*s (SDL: %s)\n", static_cast<int>(message.size()),
                     message.data(), SDL_GetError());
    }
    return condition;
}

bool write_text(const fs::path& path, std::string_view source)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(source.data(), static_cast<std::streamsize>(source.size()));
    file.close();
    return static_cast<bool>(file);
}

bool write_meta(const fs::path& path, std::string_view guid, std::string_view importer)
{
    auto meta = path;
    meta += ".meta";
    return write_text(meta, "guid = \"" + std::string(guid) + "\"\nimporter = \"" +
                                std::string(importer) + "\"\nmeta_version = 1\n");
}

std::string hash_text(std::string_view text)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char byte : text)
    {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ULL;
    }
    std::array<char, 17> output{};
    std::snprintf(output.data(), output.size(), "%016llx", static_cast<unsigned long long>(hash));
    return output.data();
}

constexpr std::string_view markup = R"rml(
<rml><head><title>MVC integration</title><style>
body { display: block; position: absolute; left: 0px; top: 0px; width: 640px;
       height: 480px; margin: 0px; font-family: body; font-size: 20px; color: #ffffff; }
#panel { display: block; position: absolute; left: 20px; top: 20px; width: 200px;
         height: 60px; }
button { display: block; width: 160px; height: 40px; background-color: #34475d; }
#bubble-button { position: absolute; left: 320px; top: 20px; width: 200px;
                 height: 60px; padding: 0px; }
#caption { display: block; position: absolute; left: 0px; top: 0px;
           width: 180px; height: 40px; }
input { display: block; position: absolute; left: 20px; top: 100px; width: 240px;
        height: 40px; background-color: #34475d; }
#label { display: block; position: absolute; left: 20px; top: 170px; }
#plain { display: block; position: absolute; left: 20px; top: 220px; }
</style></head><body>
<div id="panel"><button id="action">Add</button></div>
<button id="bubble-button"><span id="caption">Child caption</span></button>
<input type="text" id="name"/>
<p id="label">Initial</p><p id="plain">Plain</p>
</body></rml>
)rml";

struct Project
{
    fs::path root;

    ~Project()
    {
        if (!root.empty())
        {
            std::error_code error;
            fs::remove_all(root, error);
        }
    }

    bool initialize(const fs::path& showcase)
    {
        std::error_code error;
        root = fs::temp_directory_path(error);
        if (error)
        {
            return false;
        }
        root /= "engine-ui-script-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        for (const auto* directory :
             {"settings", "cache", "assets/fonts", "assets/ui", "assets/scenes"})
        {
            fs::create_directories(root / directory, error);
            if (error)
            {
                return false;
            }
        }
        for (const auto* name : {"lato-regular.ttf", "lato-regular.ttf.meta"})
        {
            if (!fs::copy_file(showcase / "assets/fonts" / name, root / "assets/fonts" / name,
                               fs::copy_options::none, error) ||
                error)
            {
                return false;
            }
        }
        constexpr std::array names = {"main", "alternate"};
        constexpr std::array document_guids = {ui_guid, alternate_guid};
        constexpr std::array scene_guids = {"29000000-0000-4000-8000-000000000003",
                                            "29000000-0000-4000-8000-000000000004"};
        for (std::size_t index = 0; index < names.size(); ++index)
        {
            const auto document = root / "assets/ui" / (std::string(names[index]) + ".rml");
            auto metadata = document;
            metadata += ".meta";
            const auto scene = root / "assets/scenes" / (std::string(names[index]) + ".scene");
            if (!write_text(document, markup) ||
                !write_text(metadata,
                            "guid = \"" + std::string(document_guids[index]) +
                                "\"\nimporter = \"ui\"\nmeta_version = 1\n[ui]\ntextures = []\n"
                                "[[ui.fonts]]\nfamily = \"body\"\n"
                                "asset = \"project/fonts/lato-regular.ttf\"\n"
                                "shader = \"builtin/shaders/font\"\n") ||
                !write_text(scene, "name = \"MVC\"\nactive_camera = 0\n"
                                   "[[nodes]]\nid = 1\nname = \"View\"\nparent = 0\n"
                                   "[[nodes.components]]\ntype = \"transform\"\n"
                                   "position = [0.0, 0.0, 0.0]\nrotation = [0.0, 0.0, 0.0]\n"
                                   "scale = [1.0, 1.0, 1.0]\n"
                                   "[[nodes.components]]\ntype = \"ui\"\ndocument = \"" +
                                       std::string(document_guids[index]) + "\"\n") ||
                !write_meta(scene, scene_guids[index], "scene"))
            {
                return false;
            }
        }
        return true;
    }
};

// The ordinary Model is changed only by Controller event callbacks. Update asserts results;
// it does not poll native UI events or implement application behavior.
constexpr std::string_view mvc_source = R"js(
import { Gameplay, Scene, Component, UiComponent, UiElement, UiController } from 'engine';
function check(value, message) { if (!value) throw new Error(message); }
class CounterController extends UiController {
    OnBind() {
        ++this.model.binds;
        this.button = this.view.FindElement('action');
        this.input = this.view.FindElement('name');
        this.label = this.view.FindElement('label');
        return this.Listen(this.button, 'click', event => {
            check(event.type === 'click' && event.target.id === 'action' &&
                  event.target.isValid, 'click target is a valid UiElement');
            ++this.model.count;
            check(this.Refresh(), 'click refresh');
        }) && this.Listen(this.input, 'change', event => {
            check(event.type === 'change' && event.target.id === 'name' &&
                  event.value === 'Ada', 'change carries the input value snapshot');
            ++this.model.changes;
            this.model.name = event.value;
            check(this.Refresh(), 'input refresh');
        }) && this.Listen(this.input, 'input', event => {
            check(event.value === 'Ada' && event.target.id === 'name',
                  'input carries the typed value snapshot');
            ++this.model.inputs;
        });
    }
    OnRefresh() {
        return this.label.SetText(`${this.model.count}:${this.model.changes}:${this.model.name}`);
    }
    OnUnbind() { ++this.model.unbinds; }
}
export default class extends Gameplay {
    OnInitialize() {
        this.scene = Scene.Load('project/scenes/main.scene');
        this.view = this.scene?.FindNode('View')?.GetComponent(UiComponent);
        check(this.view instanceof Component && this.view.IsValid(), 'ordinary UI component');
        this.model = { count: 0, changes: 0, inputs: 0, name: '', binds: 0, unbinds: 0 };
        this.controller = new CounterController(this.view, this.model);
        check(!this.controller.bound && this.controller.Bind() && this.controller.bound &&
              this.controller.Refresh(), 'bind and refresh during OnInitialize');
        check(this.controller.Bind() && this.model.binds === 1, 'Bind is idempotent');
        this.label = this.view.FindElement('label');
        check(this.label instanceof UiElement && this.label.isValid &&
              this.label.text === '0:0:' && this.view.FindElement('missing') === null,
              'synchronous document lookup and text reading');
        const removed = this.controller.button.On('click', () => {
            throw new Error('Off must remove a direct subscription');
        });
        check(removed > 0 && this.controller.button.Off(removed), 'direct On and Off');
        check(this.controller.button.On('click', 1) === 0 &&
              !this.controller.button.SetText(1) && !this.controller.input.SetValue(1) &&
              !this.view.SetVisible(1), 'invalid argument types fail safely');
        this.frame = 0;
        return true;
    }
    Update() {
        ++this.frame;
        check(this.label.isValid && this.controller.label.isValid,
              'renderer preparation preserves initialization handles');
        if (this.frame === 1)
            check(this.model.count === 0 && this.label.text === '0:0:', 'initial state');
        if (this.frame === 2)
            check(this.model.count === 1 && this.label.text === '1:0:', 'one click, one callback');
        if (this.frame === 3)
            check(this.model.changes === 1 && this.model.inputs === 1 && this.model.name === 'Ada' &&
                  this.controller.input.value === 'Ada' && this.label.text === '1:1:Ada',
                  'one text input updates model and view once');
        if (this.frame === 4) {
            this.controller.Unbind();
            this.controller.Unbind();
            check(!this.controller.bound && this.model.unbinds === 1, 'idempotent Unbind');
        }
        if (this.frame === 5) {
            check(this.model.count === 1, 'unbound controller receives no click');
            check(this.controller.Bind() && this.model.binds === 2, 'controller can bind again');
        }
        if (this.frame === 6)
            check(this.model.count === 2 && this.label.text === '2:1:Ada',
                  'rebinding creates one subscription');
    }
    OnShutdown() { this.controller?.Unbind(); }
}
)js";

std::string lifetime_source(std::string_view operation)
{
    return "const operation = '" + std::string(operation) + "';\n" + R"js(
import { Gameplay, Scene, UiComponent, UiController } from 'engine';
function check(value, message) { if (!value) throw new Error(message); }
class LifetimeController extends UiController {
    OnBind() {
        return this.Listen(this.model.button, 'click', () => {
            ++this.model.calls;
            check(this.model.calls === 1, 'queued old events are discarded');
            if (operation === 'document') {
                check(this.view.SetDocument('project/ui/alternate.rml'), 'replace document');
                check(!this.model.button.isValid, 'old document handle is invalid');
                check(this.view.FindElement('label').SetText(operation), 'new document view');
            } else if (operation === 'roundtrip') {
                check(this.view.SetDocument('project/ui/alternate.rml') &&
                      this.view.SetDocument('project/ui/main.rml'),
                      'replace A with B and restore A before querying either View');
                check(!this.model.button.isValid && !this.model.button.SetText('stale') &&
                      this.view.FindElement('action').isValid,
                      'returning to the same resource does not revive old handles');
                check(this.view.FindElement('label').SetText(operation), 'replacement A view');
            } else if (operation === 'scene') {
                const next = Scene.Load('project/scenes/alternate.scene');
                check(next && !this.model.scene.IsValid() && !this.model.button.isValid,
                      'scene replacement invalidates old handles during callback');
                check(next.FindNode('View').GetComponent(UiComponent).FindElement('label')
                          .SetText(operation), 'new scene view');
            } else if (operation === 'subtree') {
                check(this.view.FindElement('panel').SetText('removed'), 'replace child markup');
                check(!this.model.button.isValid && !this.model.button.SetText('stale'),
                      'removed elements do not retain live handles');
                check(this.view.FindElement('action') === null, 'text replacement removes child');
                check(this.view.FindElement('label').SetText(operation), 'surviving view');
            } else if (operation === 'unbind') {
                this.Unbind();
                check(this.view.FindElement('label').SetText(operation), 'unbound view remains');
            }
        }) && this.Listen(this.model.button, 'click', () => {
            throw new Error('second subscription must not run after callback invalidation');
        });
    }
    OnRefresh() { return true; }
}
export default class extends Gameplay {
    OnInitialize() {
        this.scene = Scene.Load('project/scenes/main.scene');
        this.view = this.scene?.FindNode('View')?.GetComponent(UiComponent);
        this.button = this.view?.FindElement('action');
        check(this.button?.isValid, 'find original button');
        const plain = this.view.FindElement('plain');
        check(plain.SetText('<b id="injected">plain & safe</b>') &&
              plain.text === '<b id="injected">plain & safe</b>' &&
              this.view.FindElement('injected') === null, 'SetText does not interpret markup');
        check(plain.SetVisible(false) && plain.SetVisible(true) &&
              this.button.SetEnabled(false) && this.button.SetEnabled(true) &&
              plain.SetClass('selected', true) && plain.SetClass('selected', false) &&
              plain.SetStyle('font-size', '22px') && !plain.SetImage('project/missing.png') &&
              this.view.SetVisible(false) && !this.view.visible &&
              this.view.SetVisible(true) && this.view.visible, 'View modifiers');
        const input = this.view.FindElement('name');
        check(input.SetValue('preset') && input.value === 'preset' && input.SetValue(''),
              'form value reading and writing');
        this.model = { scene: this.scene, button: this.button, calls: 0 };
        this.controller = new LifetimeController(this.view, this.model);
        return this.controller.Bind();
    }
    Update() {
        if (operation === 'component') {
            check(this.model.calls === 0 && !this.button.isValid &&
                  !this.button.SetText('stale'), 'replaced component discards old event');
            const view = this.scene.FindNode('View').GetComponent(UiComponent);
            check(view.FindElement('action').isValid &&
                  view.FindElement('label').SetText(operation), 'replacement component view');
        } else {
            check(this.model.calls === 1, 'only the first valid callback is delivered');
        }
    }
    OnShutdown() { this.controller?.Unbind(); }
}
)js";
}

std::string bubbling_source(std::string_view operation)
{
    return "const operation = '" + std::string(operation) + "';\n" + R"js(
import { Gameplay, Scene, UiComponent, UiController } from 'engine';
function check(value, message) { if (!value) throw new Error(message); }
function checkEvent(event, current) {
    check(event.type === 'click' && event.target.id === 'caption' && event.target.isValid &&
          event.currentTarget.id === current && event.currentTarget.isValid,
          'bubbling preserves the child target and reports the current listener element');
}
class BubbleController extends UiController {
    OnBind() {
        // Register the ancestor first: delivery must still follow the actual element path.
        return this.Listen(this.model.button, 'click', event => {
            checkEvent(event, 'bubble-button');
            check(operation === 'bubble-order', 'invalidated child stops parent delivery');
            check(this.model.children === this.model.parents + 1,
                  'child callback precedes its parent regardless of registration order');
            ++this.model.parents;
            this.model.calls.push('parent');
            check(this.Refresh(), 'refresh after parent callback');
        }) && this.Listen(this.model.caption, 'click', event => {
            checkEvent(event, 'caption');
            ++this.model.children;
            this.model.calls.push('child');
            if (operation === 'bubble-order' && this.model.children === 1) {
                check(this.Listen(this.model.button, 'click', later => {
                    checkEvent(later, 'bubble-button');
                    check(this.model.children === 2,
                          'a listener added during dispatch cannot receive that same event');
                    ++this.model.late;
                    this.model.calls.push('late-parent');
                    check(this.Refresh(), 'refresh after newly registered parent callback');
                }), 'register a parent listener from the child callback');
            } else if (operation === 'bubble-unbind') {
                this.Unbind();
                check(!this.bound, 'child callback unbinds the controller');
            } else if (operation === 'bubble-remove') {
                check(this.model.button.SetText('Removed') && !this.model.caption.isValid &&
                      this.model.button.isValid, 'remove the target while retaining its ancestor');
            }
            check(this.model.label.SetText(this.model.calls.join(',')), 'display callback order');
        });
    }
    OnRefresh() { return this.model.label.SetText(this.model.calls.join(',')); }
}
export default class extends Gameplay {
    OnInitialize() {
        const scene = Scene.Load('project/scenes/main.scene');
        const view = scene?.FindNode('View')?.GetComponent(UiComponent);
        this.model = { button: view?.FindElement('bubble-button'),
                       caption: view?.FindElement('caption'), label: view?.FindElement('label'),
                       calls: [], children: 0, parents: 0, late: 0 };
        check(this.model.button?.isValid && this.model.caption?.isValid, 'nested View elements');
        this.controller = new BubbleController(view, this.model);
        this.frame = 0;
        return this.controller.Bind();
    }
    Update() {
        ++this.frame;
        if (operation === 'bubble-order') {
            const first = this.frame === 1;
            check(this.model.children === (first ? 1 : 2) &&
                  this.model.parents === (first ? 1 : 2) && this.model.late === (first ? 0 : 1),
                  'each eligible listener runs once, and the event snapshot excludes new ones');
        } else {
            check(this.model.children === 1 && this.model.parents === 0,
                  'child invalidation suppresses both the parent and queued stale input');
        }
    }
    OnShutdown() { this.controller?.Unbind(); }
}
)js";
}

constexpr std::string_view no_ui_source = R"js(
import { Gameplay, Scene, UiComponent, UiController } from 'engine';
export default class extends Gameplay {
    OnInitialize() {
        const scene = Scene.Load('project/scenes/main.scene');
        const view = scene?.FindNode('View')?.GetComponent(UiComponent);
        if (!view || view.FindElement('action') !== null || view.SetVisible(true)) return false;
        const controller = new UiController(view, {});
        return !controller.Bind() && !controller.bound;
    }
}
)js";

constexpr std::string_view async_source = R"js(
import { Gameplay, Scene, UiComponent } from 'engine';
export default class extends Gameplay {
    OnInitialize() {
        const scene = Scene.Load('project/scenes/main.scene');
        const button = scene?.FindNode('View')?.GetComponent(UiComponent)?.FindElement('action');
        return button !== undefined && button !== null && button.On('click', async () => {}) > 0;
    }
}
)js";

struct Systems
{
    engine::ResourceSystem resources;
    engine::editor::AssetPipelineSystem pipeline;
    engine::WindowSystem window;
    engine::WorldSystem world;
    engine::FontSystem fonts;
    engine::UiSystem ui{window, world, fonts};
    engine::RenderSystem renderer{window, &world, &fonts, &ui};
    engine::plugins::script::ScriptSystem script{&world, &renderer, &ui};

    ~Systems()
    {
        script.shutdown();
        ui.shutdown();
        fonts.shutdown();
        renderer.shutdown();
        world.shutdown();
        window.shutdown();
        resources.shutdown();
        pipeline.shutdown();
    }

    bool add_script(engine::schema::AssetCatalogDataT& catalog, const fs::path& project,
                    std::string_view name, std::string_view source)
    {
        auto entry = std::make_unique<engine::schema::AssetEntryT>();
        entry->path = "project/scripts/" + std::string(name) + ".js";
        entry->module_path = "scripts/" + std::string(name) + ".js";
        entry->guid = "29000000-0000-4000-8000-" + hash_text(entry->path).substr(4);
        entry->kind = engine::schema::AssetKind::script;
        engine::ByteBuffer bytes;
        if (!engine::ResourceSystem::encode_resource(
                entry->guid, entry->kind,
                {reinterpret_cast<const std::byte*>(source.data()), source.size()}, bytes,
                entry->header_size))
        {
            return false;
        }
        const std::string_view payload(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        entry->content_hash = hash_text(payload);
        entry->content_size = bytes.size();
        entry->location = std::make_unique<engine::schema::FileLocationT>();
        entry->location->mount = "content";
        entry->location->path = "gen/" + entry->guid;
        entry->location->length = bytes.size();
        if (!write_text(project / "cache" / entry->location->path, payload))
        {
            return false;
        }
        catalog.entries.push_back(std::move(entry));
        return true;
    }

    bool initialize(const fs::path& project)
    {
        engine::EngineConfig config;
        config.window_title = "Engine UI script MVC integration";
        config.width = 640;
        config.height = 480;
        std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> font =
            std::make_unique<engine::plugins::font_import::FontImporter>();
        std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> document =
            std::make_unique<engine::plugins::ui_import::UiImporter>();
        if (!expect(pipeline.initialize(config), "initialize pipeline") ||
            !expect(pipeline.add_importer(font) && pipeline.add_importer(document),
                    "register font and UI importers") ||
            !expect(pipeline.build_assets(project), pipeline.last_error()))
        {
            return false;
        }
        auto catalog = pipeline.catalog();
        if (!add_script(catalog, project, "mvc", mvc_source) ||
            !add_script(catalog, project, "no-ui", no_ui_source) ||
            !add_script(catalog, project, "async", async_source))
        {
            return false;
        }
        for (const auto* operation :
             {"document", "roundtrip", "scene", "subtree", "unbind", "component"})
        {
            if (!add_script(catalog, project, operation, lifetime_source(operation)))
            {
                return false;
            }
        }
        for (const auto* operation : {"bubble-order", "bubble-unbind", "bubble-remove"})
        {
            if (!add_script(catalog, project, operation, bubbling_source(operation)))
            {
                return false;
            }
        }
        return expect(resources.initialize(config) &&
                          resources.update_catalog(catalog, {{"content", project / "cache"}}),
                      resources.last_error()) &&
               expect(window.initialize(config) && world.initialize(config) &&
                          fonts.initialize(config) && ui.initialize(config) &&
                          renderer.initialize(config) && script.initialize(config),
                      "initialize window, scene, font, UI, renderer and V8");
    }

    bool tick()
    {
        return expect(script.tick(delta_time) && script.status().running,
                      script.status().last_error);
    }

    bool frames(int count = 3)
    {
        int rendered = 0;
        for (int attempt = 0; attempt < 200 && rendered < count; ++attempt)
        {
            if (!expect(window.tick(delta_time) && !window.exit_requested(), "poll native window"))
            {
                return false;
            }
            if (window.can_render())
            {
                if (!expect(renderer.render(), "render real UI frame"))
                {
                    return false;
                }
                ++rendered;
            }
            else
            {
                SDL_Delay(10);
            }
        }
        return expect(rendered == count, "window becomes drawable");
    }

    bool start(std::string_view name)
    {
        return expect(script.start_gameplay("scripts/" + std::string(name) + ".js"),
                      script.status().last_error) &&
               expect(renderer.prepare_scene(), "prepare scene after gameplay initialization") &&
               frames();
    }

    void stop()
    {
        script.stop_gameplay();
        ui.release_scene();
        fonts.release_scene();
        renderer.release_scene();
    }

    bool label_is(std::string_view expected)
    {
        const auto* scene = world.scene();
        engine::UiElementHandle label;
        std::string value;
        return expect(scene != nullptr && !scene->nodes().empty() &&
                          ui.find_element(scene->nodes().front()->id(), "label", label) &&
                          ui.text(label, value) && value == expected,
                      "native view text matches the script model: " + std::string(expected));
    }
};

bool click(Systems& systems, float x = 60.0F, float y = 40.0F)
{
    const auto window_id = SDL_GetWindowID(systems.window.native_window());
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.windowID = window_id;
    motion.motion.x = x;
    motion.motion.y = y;
    static_cast<void>(systems.ui.process_event(motion));
    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.windowID = window_id;
    button.button.button = SDL_BUTTON_LEFT;
    button.button.down = true;
    button.button.clicks = 1;
    button.button.x = x;
    button.button.y = y;
    const bool pressed = systems.ui.process_event(button);
    button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    button.button.down = false;
    return expect(systems.ui.process_event(button) && pressed, "UI consumes SDL pointer input");
}

bool type_name(Systems& systems)
{
    if (!click(systems, 60.0F, 120.0F) ||
        !expect(SDL_TextInputActive(systems.window.native_window()), "input gains native focus"))
    {
        return false;
    }
    SDL_Event event{};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.windowID = SDL_GetWindowID(systems.window.native_window());
    event.text.text = "Ada";
    return expect(systems.ui.process_event(event), "UI consumes SDL text input");
}

bool mvc_case(Systems& systems, const fs::path& project)
{
    if (!systems.start("mvc") || !systems.tick() || !systems.label_is("0:0:") || !click(systems) ||
        !systems.tick() || !systems.label_is("1:0:") || !type_name(systems) || !systems.tick() ||
        !systems.label_is("1:1:Ada") || !systems.tick() || !click(systems) || !systems.tick() ||
        !systems.label_is("1:1:Ada") || !click(systems) || !systems.tick() ||
        !systems.label_is("2:1:Ada"))
    {
        return false;
    }
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        if (!click(systems) || !expect(systems.script.prepare_gameplay(
                                           systems.resources.catalog(),
                                           {{"content", project / "cache"}}, "scripts/mvc.js"),
                                       "prepare a replacement V8 context with queued old input"))
        {
            return false;
        }
        systems.stop();
        if (!expect(systems.world.scene() == nullptr && systems.ui.document_count() == 0,
                    "stopping releases the script scene and document") ||
            !expect(systems.script.start_prepared_gameplay(), systems.script.status().last_error) ||
            !expect(systems.renderer.prepare_scene(), "prepare restarted UI") ||
            !systems.frames() || !systems.tick() || !systems.label_is("0:0:") || !click(systems) ||
            !systems.tick() || !systems.label_is("1:0:"))
        {
            return false;
        }
    }
    systems.stop();
    return true;
}

bool lifetime_cases(Systems& systems)
{
    for (const auto* operation :
         {"document", "roundtrip", "scene", "subtree", "unbind", "component"})
    {
        if (!systems.start(operation) || !click(systems) || !click(systems))
        {
            return false;
        }
        if (std::string_view(operation) == "component")
        {
            auto* node = systems.world.scene()->nodes().front().get();
            if (!expect(node->remove_component(engine::UiComponent::component_type),
                        "remove component while its input is queued"))
            {
                return false;
            }
            auto* replacement = node->add_component<engine::UiComponent>();
            if (!expect(replacement != nullptr && replacement->set_document(main_document) &&
                            systems.ui.synchronize(),
                        "same node and document produce a new component identity"))
            {
                return false;
            }
        }
        if (!systems.tick() || !systems.label_is(operation) || !systems.tick() ||
            !systems.label_is(operation) || !systems.frames())
        {
            return false;
        }
        systems.stop();
    }
    return true;
}

bool bubbling_cases(Systems& systems)
{
    if (!systems.start("bubble-order") || !click(systems, 350.0F, 40.0F) || !systems.tick() ||
        !systems.label_is("child,parent") || !click(systems, 350.0F, 40.0F) || !systems.tick() ||
        !systems.label_is("child,parent,child,parent,late-parent") || !systems.tick())
    {
        return false;
    }
    systems.stop();
    for (const auto* operation : {"bubble-unbind", "bubble-remove"})
    {
        if (!systems.start(operation) || !click(systems, 350.0F, 40.0F) ||
            !click(systems, 350.0F, 40.0F) || !systems.tick() || !systems.label_is("child") ||
            !systems.tick() || !systems.label_is("child"))
        {
            return false;
        }
        systems.stop();
    }
    return true;
}

bool independent_documents(Systems& systems)
{
    auto* scene = systems.world.load_scene(main_scene);
    engine::UiElementHandle original;
    if (!expect(scene != nullptr &&
                    systems.ui.find_element(scene->nodes().front()->id(), "label", original),
                "prepare first document without rendering"))
    {
        return false;
    }
    auto* second = scene->create_node("Second View");
    auto* component = second == nullptr ? nullptr : second->add_component<engine::UiComponent>();
    if (!expect(component != nullptr && component->set_document(main_document) &&
                    systems.ui.synchronize() && systems.ui.document_count() == 2 &&
                    systems.ui.is_valid(original) && systems.ui.set_text(original, "preserved"),
                "adding another document retains the original View identity") ||
        !expect(scene->destroy_node(second->id()) && systems.ui.synchronize() &&
                    systems.ui.document_count() == 1 && systems.ui.is_valid(original),
                "removing another document retains the original View identity"))
    {
        return false;
    }
    auto* original_component = scene->nodes().front()->get_component<engine::UiComponent>();
    const auto revision = original_component->document_revision();
    if (!expect(original_component->set_document("project/ui/alternate.rml") &&
                    original_component->document_revision() == revision + 1 &&
                    original_component->set_document(main_document) &&
                    original_component->document_revision() == revision + 2 &&
                    !systems.ui.is_valid(original),
                "A to B to A increments the document revision and invalidates the original View"))
    {
        return false;
    }
    engine::UiElementHandle replacement;
    if (!expect(systems.ui.find_element(scene->nodes().front()->id(), "label", replacement) &&
                    systems.ui.is_valid(replacement) &&
                    replacement.document_id != original.document_id,
                "returning to A creates a fresh document identity") ||
        !expect(original_component->set_document(main_document) &&
                    original_component->document_revision() == revision + 2 &&
                    systems.ui.is_valid(replacement),
                "assigning the current document preserves its revision and live handles"))
    {
        return false;
    }
    systems.ui.release_scene();
    systems.world.destroy_scene();
    systems.renderer.release_scene();
    return expect(!systems.ui.is_valid(original), "scene release invalidates native UI handles");
}

bool missing_ui(Systems& systems)
{
    engine::plugins::script::ScriptSystem script(&systems.world);
    if (!expect(script.initialize({}) && script.start_gameplay("scripts/no-ui.js") &&
                    script.tick(delta_time) && script.status().running,
                "UI script API safely rejects View binding without an injected UiSystem"))
    {
        return false;
    }
    script.stop_gameplay();
    return expect(systems.world.scene() == nullptr,
                  "UI-less script still owns its scene lifecycle");
}

bool asynchronous_callback_rejected(Systems& systems)
{
    if (!systems.start("async") || !click(systems))
    {
        return false;
    }
    return expect(systems.script.tick(delta_time) && !systems.script.status().running &&
                      systems.script.status().last_error.find("must be synchronous") !=
                          std::string::npos &&
                      systems.world.scene() == nullptr && systems.ui.synchronize() &&
                      systems.ui.document_count() == 0,
                  "asynchronous event callback fails with clean script and UI teardown");
}
} // namespace

int main(int argc, char** argv)
{
    if (!expect(argc == 2, "provide the showcase project containing the Lato font fixture"))
    {
        return 1;
    }
    Project project;
    if (!expect(project.initialize(engine::PathUtils::from_utf8(argv[1])),
                "create isolated UI script project"))
    {
        return 1;
    }
    engine::AppConfig app;
    app.application_name = "engine-ui-script-test";
    app.builtin_assets_path = project.root / "builtin";
    app.persistent_data_path = project.root / "persistent";
    app.cache_path = project.root / "app-cache";
    if (!engine::App::initialize(app) || !engine::LogSystem::instance().initialize({}))
    {
        engine::App::shutdown();
        return 1;
    }
    bool passed = false;
    {
        Systems systems;
        passed = systems.initialize(project.root) && systems.frames() &&
                 expect(bgfx::getRendererType() != bgfx::RendererType::Noop,
                        "integration requires a real GPU backend") &&
                 mvc_case(systems, project.root) && lifetime_cases(systems) &&
                 bubbling_cases(systems) && independent_documents(systems) && missing_ui(systems) &&
                 asynchronous_callback_rejected(systems);
    }
    engine::LogSystem::instance().shutdown();
    engine::App::shutdown();
    if (passed)
    {
        std::puts(
            "UI script MVC, SDL input, View identities, subscription cleanup and reload passed.");
    }
    return passed ? 0 : 1;
}
