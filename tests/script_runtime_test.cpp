#include "runtime/app/app.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/script/script_system.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/world/world_system.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <system_error>

namespace
{

namespace fs = std::filesystem;
using Json = nlohmann::json;

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "script runtime test: %s\n", message);
    }
    return condition;
}

bool write_file(const fs::path& path, std::string_view source)
{
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error)
    {
        return false;
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(source.data(), static_cast<std::streamsize>(source.size()));
    file.close();
    return static_cast<bool>(file);
}

std::string hash_text(std::string_view source)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char byte : source)
    {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ULL;
    }
    std::array<char, 17> output{};
    std::snprintf(output.data(), output.size(), "%016llx", static_cast<unsigned long long>(hash));
    return output.data();
}

bool publish(const std::string& revision, const std::map<std::string, std::string>& files,
             const Json& deleted = Json::array(), bool valid_hash = true,
             std::string_view entry = "scripts/game.js")
{
    const fs::path root = engine::App::persistent_data_path() / "assets";
    Json description = Json::array();
    for (const auto& [path, source] : files)
    {
        if (!write_file(root / "versions" / revision / path, source))
        {
            return false;
        }
        description.push_back({{"path", path},
                               {"size", source.size()},
                               {"hash", valid_hash ? hash_text(source) : "0000000000000000"}});
    }
    const Json document = {
        {"revision", revision}, {"entry", entry}, {"files", description}, {"deleted", deleted}};
    return write_file(root / "available.json", document.dump());
}

std::string game_source(std::string_view name, int expected_value)
{
    return "import { Gameplay, Debug } from 'engine';\n"
           "import { value } from './lib/value.js';\n"
           "export default class Game extends Gameplay {\n"
           " count = 0;\n"
           " OnInitialize() { Debug.Log('" +
           std::string(name) + ":start'); return value === " + std::to_string(expected_value) +
           "; }\n"
           " Update() { Debug.Log('" +
           std::string(name) +
           ":update:' + (++this.count)); }\n"
           " OnShutdown() { Debug.Log('" +
           std::string(name) +
           ":stop'); }\n"
           "}\n";
}

bool log_contains(std::string_view message)
{
    std::string text;
    return engine::LogSystem::instance().flush() &&
           engine::ResourceSystem::read_text(engine::LogSystem::instance().file_path(), text) &&
           text.find(message) != std::string::npos;
}

bool retained(const engine::ScriptSystem& system, std::string_view revision)
{
    return expect(system.status().running && system.status().active_revision == revision &&
                      !system.status().last_error.empty(),
                  "failed candidate must preserve the old active revision and report its error");
}

bool run_cases(const fs::path& workspace)
{
    using engine::App;
    engine::ScriptSystem script;
    const engine::EngineConfig config;
    if (!expect(script.initialize(config), "initialize real V8 environment") ||
        !expect(!script.status().running, "initialization must not start gameplay"))
    {
        return false;
    }

    const std::string builtin =
        "import { Gameplay, Debug, App } from 'engine';\n"
        "import { value } from './lib/value.js';\n"
        "export default class Game extends Gameplay {\n"
        " count = 0;\n"
        " OnInitialize() {\n"
        "  Debug.Log('builtin:start'); Debug.LogWarning('binding:warning');\n"
        "  Debug.LogError('binding:error');\n"
        "  return value === 2 && App.builtinAssetsPath === " +
        Json(engine::PathUtils::to_utf8(App::builtin_assets_path())).dump() +
        " && App.persistentDataPath === " +
        Json(engine::PathUtils::to_utf8(App::persistent_data_path())).dump() +
        " && App.cachePath === " + Json(engine::PathUtils::to_utf8(App::cache_path())).dump() +
        " && Object.getOwnPropertyDescriptor(App, 'cachePath').set === undefined;\n"
        " }\n"
        " Update() { Debug.Log('builtin:update:' + (++this.count)); }\n"
        " OnShutdown() { Debug.Log('builtin:stop'); }\n"
        "}\n";
    if (!write_file(App::builtin_assets_path() / "scripts/game.js", builtin) ||
        !write_file(App::builtin_assets_path() / "scripts/lib/value.js",
                    "export const value = 2;\n") ||
        !expect(script.start_gameplay(), "builtin ES entry and relative import must execute") ||
        !expect(script.status().active_revision == "builtin", "builtin revision is reported") ||
        !expect(script.tick(0.01), "builtin update") ||
        !expect(log_contains("builtin:update:1") && log_contains("[WARNING] binding:warning") &&
                    log_contains("[ERROR] binding:error"),
                "native logging levels and Update"))
    {
        return false;
    }

    // 关热重载期间只更新 available，不能停止或重建当前入口。
    if (!publish("cached-1", {{"scripts/game.js", game_source("cached", 99)},
                              {"scripts/lib/value.js", "export const value = 99;\n"}}) ||
        !expect(script.tick(0.01), "observe new complete revision") ||
        !expect(script.status().active_revision == "builtin" &&
                    script.status().available_revision == "cached-1" && !script.status().hot_reload,
                "hot reload off must keep the active snapshot") ||
        !expect(log_contains("builtin:update:2") && !log_contains("builtin:stop"),
                "old instance must retain its state while reload is off") ||
        !write_file(App::persistent_data_path() / "script-control.json", "{\"hotReload\":true}") ||
        !expect(script.tick(0.01), "process control then reload then Update") ||
        !expect(script.status().active_revision == "cached-1" && script.status().hot_reload,
                "control file enables reload") ||
        !expect(log_contains("builtin:stop") && log_contains("cached:update:1"),
                "cached helper overrides builtin and replacement starts fresh"))
    {
        return false;
    }

    // 清单没覆盖的路径仍回退内置文件；不能把清单误当必须完整覆盖包内内容。
    if (!publish("fallback-2", {{"scripts/game.js", game_source("fallback", 2)}}) ||
        !expect(script.tick(0.01), "load per-path builtin fallback") ||
        !expect(script.status().active_revision == "fallback-2" &&
                    log_contains("fallback:update:1"),
                "relative imports share the fallback rule"))
    {
        return false;
    }

    if (!publish("corrupt-3", {{"scripts/game.js", game_source("corrupt", 2)}}, Json::array(),
                 false) ||
        !expect(script.tick(0.01), "hash mismatch is a script error, not an Engine failure") ||
        !retained(script, "fallback-2") ||
        !expect(!log_contains("fallback:stop"), "hash failure preserves old entry"))
    {
        return false;
    }

    if (!publish("syntax-4", {{"scripts/game.js", "export default class { invalid syntax }"}}) ||
        !expect(script.tick(0.01), "syntax errors do not fail the engine frame") ||
        !retained(script, "fallback-2") ||
        !expect(script.status().available_revision == "syntax-4",
                "valid file version remains available despite script compile failure") ||
        !expect(!log_contains("fallback:stop"), "syntax precheck must not shut down old entry"))
    {
        return false;
    }

    if (!publish("deleted-5", {{"scripts/game.js", game_source("deleted", 2)}},
                 Json::array({"scripts/lib/value.js"})) ||
        !expect(script.tick(0.01), "deleted import rejection") || !retained(script, "fallback-2") ||
        !expect(!log_contains("deleted:start"), "deleted helper must never fall back to builtin"))
    {
        return false;
    }

    if (!publish("escape-6",
                 {{"scripts/game.js", "import '../../outside.js'; import {Gameplay} from 'engine'; "
                                      "export default class extends Gameplay {}"}}) ||
        !expect(script.tick(0.01), "escaping import rejected") || !retained(script, "fallback-2") ||
        !expect(!script.start_gameplay("/absolute.js") && !script.start_gameplay("../../escape.js"),
                "absolute and escaping entry paths rejected") ||
        !retained(script, "fallback-2"))
    {
        return false;
    }

    // 实际文件存在且哈希正确也不能通过越界符号链接加载。
    const std::string linked_source = game_source("linked", 2);
    if (!write_file(workspace / "outside.js", linked_source) ||
        !publish("symlink-7", {{"scripts/game.js", linked_source}}))
    {
        return false;
    }
    const auto linked_path =
        App::persistent_data_path() / "assets/versions/symlink-7/scripts/game.js";
    std::error_code error;
    fs::remove(linked_path, error);
    if (error)
    {
        return false;
    }
    fs::create_symlink(workspace / "outside.js", linked_path, error);
    if (error)
    {
#ifdef _WIN32
        std::puts("SKIP: symlink creation requires Windows developer mode or permission");
#else
        return expect(false, "create outside symlink fixture");
#endif
    }
    else if (!expect(script.tick(0.01), "symlink escape rejected") ||
             !retained(script, "fallback-2"))
    {
        return false;
    }

    // ES 模块链接（缺少 export）同样在停止旧实例之前失败。
    if (!publish("link-8",
                 {{"scripts/game.js",
                   "import {missing} from './lib/value.js'; import {Gameplay} from 'engine'; "
                   "export default class extends Gameplay { OnInitialize(){return missing;} }"}}) ||
        !expect(script.tick(0.01), "missing export rejected during link") ||
        !retained(script, "fallback-2") ||
        !expect(!log_contains("fallback:stop"), "all preflight failures preserve old lifecycle"))
    {
        return false;
    }

    if (!publish("execute-9",
                 {{"scripts/game.js",
                   "import {Gameplay} from 'engine'; throw new Error('top level execution "
                   "failure'); export default class extends Gameplay {}"}}) ||
        !expect(script.tick(0.01), "execution error remains a script error") ||
        !expect(!script.status().running && script.status().active_revision.empty() &&
                    !script.status().last_error.empty(),
                "post-precheck execution failure explicitly stops gameplay") ||
        !expect(log_contains("fallback:stop"), "old entry retires before new module execution"))
    {
        return false;
    }

    if (!publish("boolean-10",
                 {{"scripts/game.js",
                   "import {Gameplay,Debug} from 'engine'; export default class extends Gameplay { "
                   "OnInitialize(){return 'true';} OnShutdown(){Debug.Log('partial:stop');} }"}}) ||
        !expect(script.tick(0.01), "invalid initialization result") ||
        !expect(!script.status().running && log_contains("partial:stop"),
                "OnInitialize requires actual bool and partial startup cleans up"))
    {
        return false;
    }

    if (!publish("update-11",
                 {{"scripts/game.js",
                   "import {Gameplay,Debug} from 'engine'; export default class extends Gameplay { "
                   "count=0; Update(){ if (++this.count === 1) throw new Error('update failure'); "
                   "Debug.Log('update:recovered'); } }"}}) ||
        !expect(script.tick(0.01), "Update exception must not fail Engine") ||
        !expect(!script.status().running && !script.status().last_error.empty(),
                "Update failure returns gameplay to idle") ||
        !expect(script.tick(0.01) && !log_contains("update:recovered"),
                "failed Update cannot run the next frame") ||
        !publish("healthy-12", {{"scripts/game.js", game_source("healthy", 2)}}) ||
        !expect(script.tick(0.01) && !script.status().running,
                "publication after Update failure must not restart gameplay") ||
        !expect(script.start_gameplay(), "explicitly start a healthy replacement") ||
        !expect(script.status().running && script.status().last_error.empty(),
                "successful replacement clears prior error"))
    {
        return false;
    }

    // 失败的显式启动不能改变仍在运行实例的后续入口选择。
    if (!expect(!script.start_gameplay("scripts/missing.js"), "missing explicit entry fails") ||
        !retained(script, "healthy-12") ||
        !publish("entry-13", {{"scripts/alternate.js", game_source("alternate", 2)}}, Json::array(),
                 true, "scripts/alternate.js") ||
        !expect(script.tick(0.01) && log_contains("alternate:update:1"),
                "empty entry follows manifest after a failed explicit start") ||
        !expect(script.start_gameplay("scripts/game.js"), "explicit builtin fallback entry") ||
        !publish("explicit-14",
                 {{"scripts/game.js", game_source("pinned", 2)},
                  {"scripts/alternate.js", game_source("ignored", 2)}},
                 Json::array(), true, "scripts/alternate.js"))
    {
        return false;
    }
    script.set_hot_reload(false);
    if (!expect(script.tick(0.01) && script.status().active_revision == "entry-13" &&
                    script.status().available_revision == "explicit-14",
                "C++ switch off keeps revision and reports available") ||
        !expect(!log_contains("pinned:start"), "reload off must not evaluate pending entry"))
    {
        return false;
    }
    script.set_hot_reload(true);
    if (!expect(script.tick(0.01) && log_contains("pinned:update:1") &&
                    !log_contains("ignored:start"),
                "explicit entry stays pinned across reload"))
    {
        return false;
    }

    script.stop_gameplay();
    script.stop_gameplay();
    std::string status_text;
    if (!expect(engine::ResourceSystem::read_text(
                    App::persistent_data_path() / "script-status.json", status_text),
                "atomic status file exists"))
    {
        return false;
    }
    const auto status = Json::parse(status_text, nullptr, false);
    if (!expect(status.is_object() && status["running"] == false &&
                    status["activeRevision"] == "" && status["availableRevision"] == "explicit-14",
                "status file reflects stop") ||
        !publish("stopped-13", {{"scripts/game.js", game_source("stopped", 2)}}) ||
        !expect(script.tick(0.01) && !script.status().running && !log_contains("stopped:start"),
                "new publication must not automatically restart explicitly stopped gameplay"))
    {
        return false;
    }

    script.shutdown();
    fs::remove(App::persistent_data_path() / "assets/available.json", error);
    if (error)
    {
        return false;
    }
    // 同一进程重新创建 isolate，不能 Dispose 后重新 Initialize V8 全局平台。
    if (!expect(script.initialize(config) && script.start_gameplay() && script.tick(0.01),
                "system restart recreates isolate with the process platform still ready"))
    {
        return false;
    }
    script.shutdown();
    script.shutdown();
    return true;
}

const std::string scene_source = R"js(
import { Gameplay, Scene, Time, Debug, Component } from 'engine';
const topLevelBlocked = Scene.Create('top-level') === null && Time.deltaTime === 0;
export default class Game extends Gameplay {
    cube = null;
    scene = null;
    angle = 0;
    constructor() {
        super();
        this.constructorBlocked = Scene.Create('constructor') === null;
    }
    OnInitialize() {
        if (!topLevelBlocked || !this.constructorBlocked || Time.deltaTime !== 0) return false;
        const discarded = Scene.Create('discarded');
        const oldNode = discarded?.CreateNode('old');
        const oldComponent = oldNode?.AddMeshRenderer();
        if (!discarded || !oldNode || !oldComponent || !discarded.Destroy()) return false;
        this.scene = Scene.Create('bound-scene');
        if (!this.scene || discarded.IsValid() || oldNode.IsValid() || oldComponent.IsValid() ||
            oldNode.SetPosition(1, 2, 3) || oldNode.AddCamera() !== null || discarded.Destroy() ||
            oldComponent.SetEnabled(false)) return false;
        const cube = this.scene.CreateNode('cube');
        const cameraNode = this.scene.CreateNode('camera');
        const lightNode = this.scene.CreateNode('light');
        if (!cube || !cameraNode || !lightNode || !cube.IsValid()) return false;
        const mesh = cube.AddMeshRenderer();
        const camera = cameraNode.AddCamera();
        const light = lightNode.AddLight();
        if (!mesh || !camera || !light || !(mesh instanceof Component) || !mesh.node.IsValid())
            return false;
        if (cube.AddMeshRenderer() !== null || cube.SetPosition(NaN, 0, 0) ||
            cube.SetScale(0, 1, 1) || camera.SetPerspective(0, 1, 2) ||
            light.SetIntensity(-1) || light.SetColor(Infinity, 1, 1)) return false;
        if (!cube.SetPosition(1, 2, 3) || !cube.SetScale(2, 2, 2) ||
            !mesh.SetMaterial({baseColor:{r:.2,g:.3,b:.4,a:1},metallic:.7,roughness:.2,unlit:false}) ||
            mesh.SetMaterial({baseColor:{r:2,g:.3,b:.4,a:1},metallic:.7,roughness:.2,unlit:false}) ||
            !mesh.SetEnabled(false) || !mesh.SetEnabled(true) ||
            !cameraNode.SetPosition(0, 0, 8) || !camera.SetPerspective(45, .1, 200) ||
            !this.scene.SetActiveCamera(camera) || !light.SetColor(1, .9, .8) ||
            !light.SetIntensity(5) || !lightNode.SetRotation(-30, 20, 0)) return false;
        const parent = this.scene.CreateNode('temporary-parent');
        const child = this.scene.CreateNode('temporary-child');
        const component = child?.AddLight();
        if (!parent || !child || !component || !child.SetParent(parent) || parent.SetParent(child) ||
            !parent.Destroy() || child.IsValid() || component.IsValid() || child.Destroy() ||
            component.SetIntensity(1) || this.scene.SetActiveCamera(component)) return false;
        if (Scene.Create('duplicate') !== null) return false;
        this.cube = cube;
        Debug.Log('scene:initialized');
        return true;
    }
    Update() {
        if (!(Time.deltaTime > 0)) throw new Error('missing deltaTime');
        this.angle += Time.deltaTime * 10;
        if (!this.cube.SetRotation(0, this.angle, 0)) throw new Error('stale cube');
    }
    OnShutdown() {
        if (Time.deltaTime !== 0 || !this.scene.IsValid() || !this.cube.SetScale(1, 1, 1))
            throw new Error('scene must survive through OnShutdown');
        Debug.Log('scene:shutdown-valid');
    }
}
)js";

bool run_scene_cases()
{
    using engine::App;
    std::error_code error;
    fs::remove(App::persistent_data_path() / "assets/available.json", error);
    if (error)
    {
        return false;
    }
    fs::remove(App::persistent_data_path() / "script-control.json", error);
    if (error)
    {
        return false;
    }
    engine::WorldSystem world;
    engine::ScriptSystem script(&world);
    const engine::EngineConfig config;
    if (!expect(world.initialize(config) && script.initialize(config),
                "initialize scene binding") ||
        !write_file(App::builtin_assets_path() / "scripts/game.js", scene_source) ||
        !expect(script.start_gameplay(), "create native scene, nodes and all component bindings") ||
        !expect(world.scene() != nullptr && world.scene()->nodes().size() == 3,
                "destroyed node subtree is removed; only cube, camera and light remain"))
    {
        return false;
    }
    const auto* cube = world.scene()->nodes().front().get();
    if (!expect(cube->name() == "cube" && cube->transform().position.x == 1.0 &&
                    cube->transform().scale.x == 2.0 && cube->mesh_renderer() != nullptr &&
                    cube->mesh_renderer()->material().metallic == 0.7F &&
                    cube->mesh_renderer()->material().base_color.r == 0.2F &&
                    world.scene()->active_camera() != nullptr &&
                    world.scene()->active_camera()->settings().fov_degrees == 45.0F,
                "native scene retains validated transforms, material and active camera") ||
        !expect(script.tick(0.25) && cube->transform().rotation.y == 2.5,
                "Time.deltaTime drives the native cube transform"))
    {
        return false;
    }
    script.set_hot_reload(true);
    const auto original_scene = world.scene()->id();
    if (!publish("scene-bad-precheck", {{"scripts/game.js", "export default class { ??? }"}}) ||
        !expect(script.tick(0.1) && script.status().running && world.scene() != nullptr &&
                    world.scene()->id() == original_scene,
                "failed precheck preserves the exact native scene") ||
        !publish("scene-reload", {{"scripts/game.js", scene_source}}) ||
        !expect(script.tick(0.1) && script.status().running && world.scene() != nullptr &&
                    world.scene()->id() != original_scene && world.scene()->nodes().size() == 3 &&
                    log_contains("scene:shutdown-valid"),
                "successful reload retires old scene after its shutdown callback"))
    {
        return false;
    }
    script.stop_gameplay();
    script.stop_gameplay();
    if (!expect(world.scene() == nullptr && !script.status().running &&
                    script.status().last_error.empty(),
                "explicit stop destroys scene after valid OnShutdown") ||
        !publish("scene-stopped", {{"scripts/game.js", scene_source}}) ||
        !expect(script.tick(0.1) && world.scene() == nullptr && !script.status().running,
                "hot reload cannot restart explicitly stopped scene") ||
        !expect(script.start_gameplay(), "restart scene explicitly"))
    {
        return false;
    }
    const std::string partial =
        "import {Gameplay,Scene,Debug} from 'engine'; export default class extends Gameplay { "
        "s=null; OnInitialize(){this.s=Scene.Create('partial'); this.s.CreateNode('partial-node'); "
        "return false;} OnShutdown(){if(this.s.IsValid()) Debug.Log('partial-scene:shutdown');} }";
    if (!publish("scene-partial", {{"scripts/game.js", partial}}) ||
        !expect(script.tick(0.1) && !script.status().running && world.scene() == nullptr &&
                    log_contains("partial-scene:shutdown"),
                "failed initialization invokes shutdown then destroys partial native scene"))
    {
        return false;
    }
    const std::string update_error =
        "import {Gameplay,Scene,Debug} from 'engine'; export default class extends Gameplay { "
        "s=null; OnInitialize(){this.s=Scene.Create('bad-update'); return this.s!==null;} "
        "Update(){throw new Error('scene-update-failed');} "
        "OnShutdown(){if(this.s.IsValid()) Debug.Log('failed-scene:shutdown');} }";
    if (!publish("scene-update-error", {{"scripts/game.js", update_error}}) ||
        !expect(script.start_gameplay() && script.tick(0.1) && !script.status().running &&
                    world.scene() == nullptr && log_contains("failed-scene:shutdown") &&
                    script.status().last_error.find("scene-update-failed") != std::string::npos,
                "Update error clears scene and reports failure without failing engine tick") ||
        !publish("scene-after-error", {{"scripts/game.js", scene_source}}) ||
        !expect(script.tick(0.1) && !script.status().running && world.scene() == nullptr,
                "publication cannot restart gameplay after Update error"))
    {
        return false;
    }
    const std::string stale = R"js(
import {Gameplay,Scene,Debug} from 'engine';
export default class extends Gameplay {
    scene=null; node=null; mesh=null;
    OnInitialize(){this.scene=Scene.Create('external-test'); this.node=this.scene.CreateNode('owned');
        this.mesh=this.node.AddMeshRenderer(); return this.mesh!==null;}
    Update(){if(this.scene.IsValid() || this.node.IsValid() || this.mesh.IsValid() ||
        this.node.SetPosition(9,9,9) || this.mesh.SetEnabled(false) || this.scene.Destroy() ||
        Scene.Create('steal')!==null) throw new Error('stale references were accepted');
        Debug.Log('stale:rejected');}
    OnShutdown(){Debug.Log('stale:shutdown');}
}
)js";
    if (!publish("scene-external", {{"scripts/game.js", stale}}) ||
        !expect(script.start_gameplay(), "start external invalidation fixture"))
    {
        return false;
    }
    world.destroy_scene();
    auto* foreign = world.create_scene("foreign");
    if (!expect(foreign != nullptr && script.tick(0.1) && log_contains("stale:rejected") &&
                    world.scene() == foreign && foreign->nodes().empty(),
                "stale scene/node/component references cannot touch a replacement scene"))
    {
        return false;
    }
    script.stop_gameplay();
    if (!expect(world.scene() == foreign, "script cleanup must not destroy an unowned scene"))
    {
        return false;
    }
    world.destroy_scene();
    script.shutdown();
    world.shutdown();
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "Usage: engine_script_runtime_test <test-output-directory>\n");
        return 1;
    }
    std::error_code error;
    const auto workspace = fs::absolute(engine::PathUtils::from_utf8(argv[1]), error);
    if (error || workspace == workspace.root_path())
    {
        return 1;
    }
    fs::remove_all(workspace, error);
    if (error)
    {
        return 1;
    }
    engine::AppConfig app;
    app.application_name = "engine-script-runtime-test";
    app.builtin_assets_path = workspace / fs::path(u8"builtin-资源");
    app.persistent_data_path = workspace / fs::path(u8"data-持久");
    app.cache_path = workspace / fs::path(u8"cache-缓存");
    if (!engine::App::initialize(app))
    {
        return 1;
    }
    const engine::EngineConfig config;
    if (!engine::LogSystem::instance().initialize(config))
    {
        engine::App::shutdown();
        return 1;
    }
    const bool passed = run_cases(workspace) && run_scene_cases();
    engine::LogSystem::instance().shutdown();
    engine::App::shutdown();
    if (passed)
    {
        std::puts("Script runtime PASS: ES modules, scenes, components, snapshots, hot reload and "
                  "failures");
    }
    return passed ? 0 : 1;
}
