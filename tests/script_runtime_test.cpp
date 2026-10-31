#include "plugins/runtime/script/script_system.hpp"
#include "runtime/app/app.hpp"
#include "runtime/engine.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/audio_listener.hpp"
#include "runtime/scene/audio_source.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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

struct Fixture
{
    fs::path root;
    engine::ResourceCatalog catalog;
    engine::MountList mounts;
};

std::string target_platform()
{
#if defined(__APPLE__)
    return "macos";
#elif defined(_WIN32)
    return "windows";
#else
    return "linux";
#endif
}

std::string guid_for(std::string_view path)
{
    return "10000000-0000-4000-8000-" + hash_text(path).substr(4);
}

bool fixture(const fs::path& workspace, std::string_view revision,
             const std::map<std::string, std::string>& scripts, Fixture& output,
             std::string_view scene_bytes = {}, std::string_view audio_bytes = {})
{
    output = {};
    output.root = workspace / "versions" / engine::PathUtils::from_utf8(revision);
    output.mounts = {{"content", output.root}};
    engine::schema::AssetCatalogDataT data;
    data.schema_version = 2;
    data.revision = revision;
    data.target_platform = target_platform();
    const auto add = [&](std::string path, std::string module, engine::schema::AssetKind kind,
                         std::string_view payload)
    {
        auto entry = std::make_unique<engine::schema::AssetEntryT>();
        entry->guid = guid_for(path);
        entry->path = std::move(path);
        entry->kind = kind;
        entry->module_path = std::move(module);
        engine::ByteBuffer entity;
        if (!engine::ResourceSystem::encode_resource(
                entry->guid, kind,
                {reinterpret_cast<const std::byte*>(payload.data()), payload.size()}, entity,
                entry->header_size))
        {
            return false;
        }
        const std::string text(reinterpret_cast<const char*>(entity.data()), entity.size());
        entry->content_hash = hash_text(text);
        entry->content_size = entity.size();
        entry->location = std::make_unique<engine::schema::FileLocationT>();
        entry->location->mount = "content";
        entry->location->path = "gen/" + entry->guid;
        entry->location->length = entity.size();
        if (!write_file(output.root / engine::PathUtils::from_utf8(entry->location->path), text))
        {
            return false;
        }
        data.entries.push_back(std::move(entry));
        return true;
    };
    for (const auto& [module, source] : scripts)
    {
        if (!add("project/" + module, module, engine::schema::AssetKind::script, source))
        {
            return false;
        }
    }
    if (!scene_bytes.empty() &&
        !add("project/scenes/main.scene", "", engine::schema::AssetKind::scene, scene_bytes))
    {
        return false;
    }
    if (!audio_bytes.empty() &&
        !add("project/sounds/test.wav", "", engine::schema::AssetKind::audio, audio_bytes))
    {
        return false;
    }
    return expect(output.catalog.assign(data), "assign runtime fixture metadata");
}

bool apply(engine::plugins::script::ScriptSystem& script, const Fixture& candidate,
           std::string_view entry = "scripts/game.js")
{
    if (!script.prepare_gameplay(candidate.catalog, candidate.mounts, entry))
    {
        return false;
    }
    script.stop_gameplay();
    auto* resources = engine::ResourceSystem::instance();
    return resources != nullptr &&
           resources->update_catalog(candidate.catalog.data(), candidate.mounts) &&
           script.start_prepared_gameplay();
}

bool log_contains(std::string_view message)
{
    auto& log = engine::LogSystem::instance();
    if (!log.flush())
    {
        return false;
    }
    // 日志仍由 ofstream 写入；使用兼容共享读取的文件流，而非只读资源的 SDL 文件接口。
    std::ifstream file(log.file_path(), std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return file.is_open() && !file.bad() && text.find(message) != std::string::npos;
}

std::string game_source(std::string_view name, int expected_value)
{
    return "import {Gameplay,Debug} from 'engine'; import {value} from './lib/value.js';"
           "export default class extends Gameplay { count=0; OnInitialize(){Debug.Log('" +
           std::string(name) + ":start');return value===" + std::to_string(expected_value) +
           ";} Update(){Debug.Log('" + std::string(name) +
           ":update:'+ ++this.count);} "
           "OnShutdown(){Debug.Log('" +
           std::string(name) + ":stop');}}";
}

bool run_cases(const fs::path& workspace)
{
    engine::plugins::script::ScriptSystem script;
    Fixture current;
    if (!expect(script.initialize({}), "initialize selected JavaScript VM") ||
        !fixture(workspace, "first",
                 {{"scripts/game.js", game_source("first", 2)},
                  {"scripts/lib/value.js", "export const value=2;"}},
                 current) ||
        !expect(apply(script, current) && script.tick(.01) && log_contains("first:update:1"),
                "explicit catalog, ES imports and native logging execute"))
    {
        return false;
    }
    Fixture next;
    if (!fixture(workspace, "second",
                 {{"scripts/game.js", game_source("second", 3)},
                  {"scripts/lib/value.js", "export const value=3;"}},
                 next) ||
        !expect(script.prepare_gameplay(next.catalog, next.mounts),
                "prepare candidate without switching") ||
        !expect(script.status().active_revision == "first" && script.tick(.01) &&
                    log_contains("first:update:2") && !log_contains("first:stop"),
                "candidate precheck neither stops old gameplay nor changes core directory") ||
        !expect(
            engine::ResourceSystem::instance()->update_catalog(next.catalog.data(), next.mounts) &&
                engine::ResourceSystem::instance()->catalog().data().revision == "second" &&
                script.status().active_revision == "first" && script.tick(.01) &&
                log_contains("first:update:3") && !log_contains("first:stop") &&
                !log_contains("second:start"),
            "publishing a new directory keeps the old scripts running until explicit reload"))
    {
        return false;
    }
    for (const auto& [name, source] : std::map<std::string, std::string>{
             {"syntax", "export default class { ??? }"},
             {"missing", "import './missing.js'; export default class {}"},
             {"escape", "import '../../outside.js'; export default class {}"},
             {"exports", "import {missing} from './lib/value.js'; export default class {}"},
             {"nul-import", R"js(import './lib/value.js\u0000ignored'; export default class {})js"},
             {"nul-export", R"js(export {value} from './lib/value.js\u0000ignored';
                                export default class {})js"},
             {"import-attributes", R"js(import './lib/value.js' with {type:'json'};
                                       export default class {})js"},
             {"export-attributes", R"js(export {value} from './lib/value.js' with {type:'json'};
                                       export default class {})js"},
             {"await", "import {Gameplay} from 'engine'; await Promise.resolve(); export default "
                       "class extends Gameplay{}"}})
    {
        Fixture bad;
        if (!fixture(
                workspace, name,
                {{"scripts/game.js", source}, {"scripts/lib/value.js", "export const value=9;"}},
                bad) ||
            !expect(!script.prepare_gameplay(bad.catalog, bad.mounts) && script.status().running &&
                        script.status().active_revision == "first" && !log_contains("first:stop"),
                    "failed precheck preserves the old running context"))
        {
            return false;
        }
    }
    if (!expect(!script.prepare_gameplay(next.catalog, next.mounts, "../outside.js") &&
                    !script.start_gameplay("/absolute.js"),
                "unsafe entry paths are rejected") ||
        !expect(apply(script, next) && script.tick(.01) && log_contains("first:stop") &&
                    log_contains("second:update:1"),
                "explicit application retires old code first"))
    {
        return false;
    }
    script.set_hot_reload(true);
    if (!write_file(engine::App::persistent_data_path() / "assets/available.json", "invalid") ||
        !write_file(engine::App::persistent_data_path() / "script-control.json", "invalid") ||
        !expect(script.tick(.01) && script.status().running && script.status().last_error.empty(),
                "script ticking does not discover or parse resource deployment files"))
    {
        return false;
    }
    for (const auto& [name, source] : std::map<std::string, std::string>{
             {"plain-class", "export default class {}"},
             {"non-class", "export default 1;"},
             {"missing-default", "export const value=1;"},
             {"execute", "import {Gameplay} from 'engine';throw new Error('execution "
                         "failed');export default class extends Gameplay{}"},
             {"boolean",
              "import {Gameplay,Debug} from 'engine';export default class extends "
              "Gameplay{OnInitialize(){return 'true';}OnShutdown(){Debug.Log('partial:stop');}}"}})
    {
        Fixture bad;
        if (!fixture(workspace, name, {{"scripts/game.js", source}}, bad) ||
            !expect(!apply(script, bad) && !script.status().running &&
                        !script.status().last_error.empty(),
                    "post-precheck failure leaves gameplay stopped"))
        {
            return false;
        }
    }
    Fixture update;
    if (!fixture(
            workspace, "update",
            {{"scripts/game.js", "import {Gameplay} from 'engine'; export default class extends "
                                 "Gameplay{Update(){throw new Error('update failed');}}"}},
            update) ||
        !expect(apply(script, update) && script.tick(.01) && !script.status().running &&
                    script.status().last_error.find("update failed") != std::string::npos,
                "Update failure is reported and stops gameplay without failing the engine tick") ||
        !expect(script.tick(.01) && !script.status().running,
                "stopped gameplay never restarts itself"))
    {
        return false;
    }
    script.shutdown();
    if (!expect(script.initialize({}) && apply(script, next) && script.tick(.01),
                "script runtime can shut down and initialize again"))
    {
        return false;
    }
    script.stop_gameplay();
    script.stop_gameplay();
    script.shutdown();
    return true;
}

bool run_module_contract_cases(const fs::path& workspace)
{
    engine::plugins::script::ScriptSystem script;
    Fixture circular;
    if (!script.initialize({}) ||
        !fixture(workspace, "module-cycle",
                 {{"scripts/game.js",
                   "import {Gameplay,Debug} from 'engine';import {read} from './a.js';"
                   "Debug.Log('module-cycle:entry');export default class extends Gameplay{"
                   "OnInitialize(){return read()===12;}}"},
                  {"scripts/a.js", "import {Debug} from 'engine';import {value} from './b.js';"
                                   "Debug.Log('module-cycle:a');export const base=7;"
                                   "export function read(){return value();}"},
                  {"scripts/b.js",
                   "import {Debug} from 'engine';import {base} from './a.js';"
                   "Debug.Log('module-cycle:b');export function value(){return base+5;}"}},
                 circular) ||
        !expect(script.prepare_gameplay(circular.catalog, circular.mounts) &&
                    !script.status().running && !log_contains("module-cycle:entry") &&
                    !log_contains("module-cycle:a") && !log_contains("module-cycle:b"),
                "cyclic module precheck resolves the graph without evaluating any top level") ||
        !expect(engine::ResourceSystem::instance()->update_catalog(circular.catalog.data(),
                                                                   circular.mounts) &&
                    script.start_prepared_gameplay() && log_contains("module-cycle:entry") &&
                    log_contains("module-cycle:a") && log_contains("module-cycle:b"),
                "cyclic ES imports execute once started and preserve live bindings"))
    {
        return false;
    }
    for (const bool cycle : {false, true})
    {
        Fixture asynchronous;
        const std::string tail =
            std::string(cycle ? "import '../game.js';" : "") +
            "import {Debug} from 'engine';Debug.Log('transitive-await:executed');"
            "await Promise.resolve();export const value=1;";
        if (!fixture(workspace, cycle ? "cycle-await" : "transitive-await",
                     {{"scripts/game.js", "import {Gameplay} from 'engine';import './lib/await.js';"
                                          "export default class extends Gameplay{}"},
                      {"scripts/lib/await.js", tail}},
                     asynchronous) ||
            !expect(!script.prepare_gameplay(asynchronous.catalog, asynchronous.mounts) &&
                        script.status().running &&
                        script.status().active_revision == "module-cycle" &&
                        !log_contains("transitive-await:executed") && script.tick(.01),
                    "transitive top-level await is rejected before evaluation, including cycles"))
        {
            return false;
        }
    }
    script.stop_gameplay();
    return true;
}

bool run_job_contract_cases(const fs::path& workspace)
{
    engine::plugins::script::ScriptSystem script;
    if (!script.initialize({}))
    {
        return false;
    }
    for (const bool cached : {false, true})
    {
        Fixture dynamic;
        const std::string source =
            std::string(cached ? "import './lib/value.js';" : "") +
            "import {Gameplay} from 'engine';export default class extends Gameplay{"
            "OnInitialize(){import('./lib/value.js').catch(()=>{});return true;}}";
        if (!fixture(
                workspace, cached ? "dynamic-cached" : "dynamic-uncached",
                {{"scripts/game.js", source}, {"scripts/lib/value.js", "export const value=1;"}},
                dynamic) ||
            !expect(!apply(script, dynamic) && !script.status().running &&
                        !script.status().last_error.empty(),
                    "dynamic import is unsupported even when cached and its rejection is caught"))
        {
            return false;
        }
    }
    Fixture handled;
    if (!fixture(workspace, "promise-handled",
                 {{"scripts/game.js",
                   "import {Gameplay,Debug} from 'engine';export default class extends Gameplay{"
                   "OnInitialize(){Promise.reject(new Error('handled-initialize'))"
                   ".catch(()=>Debug.Log('jobs:handled:initialize'));return true;}"
                   "Update(){Promise.reject(new Error('handled-update'))"
                   ".catch(()=>Debug.Log('jobs:handled:update'));}}"}},
                 handled) ||
        !expect(apply(script, handled) && log_contains("jobs:handled:initialize") &&
                    script.tick(.01) && log_contains("jobs:handled:update") &&
                    script.status().running && script.status().last_error.empty(),
                "rejections handled during the same job checkpoint do not stop gameplay"))
    {
        return false;
    }
    Fixture rejection;
    if (!fixture(workspace, "promise-rejection",
                 {{"scripts/game.js",
                   "import {Gameplay,Debug} from 'engine';export default class extends Gameplay{"
                   "Update(){Promise.reject(new Error('unhandled-job-marker'));}"
                   "OnShutdown(){Debug.Log('jobs:rejected:shutdown');}}"}},
                 rejection) ||
        !expect(apply(script, rejection) && script.tick(.01) && !script.status().running &&
                    script.status().last_error.find("unhandled-job-marker") != std::string::npos &&
                    log_contains("jobs:rejected:shutdown"),
                "unhandled Promise rejection stops gameplay and still runs OnShutdown"))
    {
        return false;
    }
    constexpr std::string_view old_source = R"js(
import {Gameplay,Debug} from 'engine';
globalThis.oldRunMarker = true;
export default class extends Gameplay {
    count=0;
    OnInitialize() {
        Promise.resolve().then(()=>Debug.Log('jobs:old:'+ ++this.count));
        return true;
    }
    Update() {
        if (this.count !== 1) throw new Error('initial job was not drained');
        Promise.resolve().then(()=>Debug.Log('jobs:old:'+ ++this.count));
    }
    OnShutdown() {
        Promise.resolve().then(()=>Debug.Log('jobs:old:shutdown'));
    }
}
)js";
    Fixture old;
    if (!fixture(workspace, "jobs-old", {{"scripts/game.js", std::string(old_source)}}, old) ||
        !expect(apply(script, old) && log_contains("jobs:old:1") && script.tick(.01) &&
                    script.status().running && log_contains("jobs:old:2"),
                "ordinary Promise jobs drain after their own gameplay callback"))
    {
        return false;
    }
    constexpr std::string_view next_source = R"js(
import {Gameplay,Debug} from 'engine';
export default class extends Gameplay {
    count=0;
    OnInitialize() {
        if (globalThis.oldRunMarker !== undefined) return false;
        Promise.resolve().then(()=>Debug.Log('jobs:new:'+ ++this.count));
        return true;
    }
    Update() {
        if (this.count !== 1 || globalThis.oldRunMarker !== undefined)
            throw new Error('old context or jobs leaked into the replacement');
    }
}
)js";
    Fixture next;
    if (!fixture(workspace, "jobs-new", {{"scripts/game.js", std::string(next_source)}}, next) ||
        !expect(apply(script, next) && log_contains("jobs:old:shutdown") &&
                    log_contains("jobs:new:1") && script.tick(.01) && script.tick(.01) &&
                    script.status().running && script.status().active_revision == "jobs-new" &&
                    !log_contains("jobs:old:3"),
                "reload drains shutdown jobs and isolates the next context and Promise queue"))
    {
        return false;
    }
    script.stop_gameplay();
    return true;
}

bool run_native_boundary_cases(const fs::path& workspace)
{
    constexpr std::string_view source = R"js(
import { Gameplay } from 'engine';
export default class extends Gameplay {
    OnInitialize() {
        const native = globalThis.__engineNative;
        const scene = native.SceneCreate('boundary\0scene');
        const node = native.NodeCreate(scene, 'node\0suffix');
        if (typeof scene !== 'bigint' || typeof node !== 'bigint' ||
            !native.SceneValid(scene) || !native.NodeValid(scene, node) ||
            native.SceneFindNode(scene, 'node\0suffix') !== node ||
            native.SceneFindNode(scene, 'node') !== null) return false;
        const overflow = 1n << 64n;
        // Native identities must not be coerced to Number or truncated modulo uint64.
        for (const alias of [Number(scene), scene + overflow, scene - overflow, -1n]) {
            if (native.SceneValid(alias) || native.SceneDestroy(alias)) return false;
        }
        for (const alias of [Number(node), node + overflow, node - overflow, -1n]) {
            if (native.NodeValid(scene, alias) || native.NodeDestroy(scene, alias) ||
                native.NodeParent(scene, node, alias)) return false;
        }
        return native.SceneValid(scene) && native.NodeValid(scene, node) &&
            native.NodeParent(scene, node, 0n);
    }
}
)js";
    engine::WorldSystem world;
    engine::plugins::script::ScriptSystem script(&world);
    Fixture input;
    if (!world.initialize({}) || !script.initialize({}) ||
        !fixture(workspace, "native-boundaries", {{"scripts/game.js", std::string(source)}},
                 input) ||
        !expect(apply(script, input),
                "BigInt identities reject numeric and overflowing aliases without side effects"))
    {
        return false;
    }
    constexpr char scene_name[] = "boundary\0scene";
    constexpr char node_name[] = "node\0suffix";
    const auto* scene = world.scene();
    if (!expect(scene != nullptr && scene->nodes().size() == 1 &&
                    scene->name() == std::string_view(scene_name, sizeof(scene_name) - 1) &&
                    scene->nodes().front()->name() ==
                        std::string_view(node_name, sizeof(node_name) - 1),
                "embedded NUL survives JavaScript conversion and native name lookup"))
    {
        return false;
    }
    script.stop_gameplay();
    return expect(world.scene() == nullptr, "boundary fixture releases its own scene");
}

const std::string scene_source = R"js(
import { Gameplay, Scene, Time, Debug, Component, Camera, Light, MeshRenderer, Transform } from 'engine';
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
        const oldComponent = oldNode?.AddComponent(MeshRenderer);
        const oldTransform = oldNode?.GetComponent(Transform);
        if (!discarded || !oldNode || !oldComponent || !discarded.Destroy()) return false;
        this.scene = Scene.Create('bound-scene');
        if (!this.scene || discarded.IsValid() || oldNode.IsValid() || oldComponent.IsValid() ||
            oldTransform.SetPosition(1, 2, 3) || oldNode.GetComponent(Transform) !== null || oldNode.AddComponent(Camera) !== null || discarded.Destroy() ||
            oldComponent.SetEnabled(false)) return false;
        const cube = this.scene.CreateNode('cube');
        const cameraNode = this.scene.CreateNode('camera');
        const lightNode = this.scene.CreateNode('light');
        if (!cube || !cameraNode || !lightNode || !cube.IsValid()) return false;
        const mesh = cube.AddComponent(MeshRenderer);
        const camera = cameraNode.AddComponent(Camera);
        const light = lightNode.AddComponent(Light);
        if (!mesh || !camera || !light || !(mesh instanceof Component) || !mesh.node.IsValid())
            return false;
        if (!(cube.GetComponent(Transform) instanceof Transform) ||
            !(cube.GetComponent(MeshRenderer) instanceof MeshRenderer) ||
            cube.GetComponent(Camera) !== null || cube.GetComponent(Light) !== null ||
            cube.AddComponent(Transform) !== null || cube.AddComponent(Component) !== null ||
            cube.GetComponent(Component) !== null || cube.AddComponent(class {}) !== null ||
            cube.GetComponent({prototype: Camera.prototype}) !== null ||
            cube.AddComponent(null) !== null || cube.GetComponent(undefined) !== null) return false;
        if (cube.AddComponent(MeshRenderer) !== null || cube.GetComponent(Transform).SetPosition(NaN, 0, 0) ||
            cube.GetComponent(Transform).SetScale(0, 1, 1) || camera.SetPerspective(0, 1, 2) ||
            light.SetIntensity(-1) || light.SetColor(Infinity, 1, 1)) return false;
        if (camera.SetOrthographic(0, .1, 100) || !camera.SetOrthographic(3, .2, 100) ||
            light.SetPoint(-1) || !light.SetPoint(12) || light.SetSpot(10, 40, 20) ||
            !light.SetSpot(12, 15, 35) || !light.SetDirectional() ||
            mesh.materialCount !== 1 || mesh.GetMaterial(1) !== null ||
            mesh.GetMaterial(-1) !== null || mesh.GetMaterial(0.5) !== null ||
            mesh.SetMaterialCount(0) || mesh.SetMaterialCount(17) ||
            !mesh.SetMaterialCount(2) || mesh.materialCount !== 2) return false;
        const slot = mesh.GetMaterial(1);
        if (!slot || !slot.IsValid() || !slot.SetType('builtin/materials/default-unlit') ||
            !slot.SetV4('baseColor', .1, .2, .3, 1) ||
            !slot.SetTexture('baseColorTexture', 'builtin/textures/white-srgb') ||
            slot.GetTexture('baseColorTexture') !== 'builtin/textures/white-srgb' ||
            slot.GetTexture('missing') !== null ||
            slot.SetTexture('missing', 'builtin/textures/white-srgb') ||
            slot.SetTexture('baseColorTexture', 'builtin/meshes/cube') ||
            !mesh.SetMaterialCount(1) || slot.IsValid() || slot.SetFloat('roughness', .4) ||
            slot.GetTexture('baseColorTexture') !== null || mesh.materialCount !== 1 ||
            !mesh.SetMesh('builtin/meshes/cube') || mesh.SetMesh('project/missing') ||
            !mesh.material.SetTexture('baseColorTexture', 'builtin/textures/white-srgb')) return false;
        if (!cube.GetComponent(Transform).SetPosition(1, 2, 3) || !cube.GetComponent(Transform).SetScale(2, 2, 2) ||
            !mesh.material.SetShader('builtin/shaders/unlit') || mesh.material.SetShader('project/shaders/missing') ||
            !mesh.material.SetType('builtin/materials/default-unlit') || mesh.material.SetFloat('metallic', .5) ||
            !mesh.material.SetType('builtin/materials/default-pbr') || !mesh.material.SetV4('baseColor', .2, .3, .4, 1) ||
            !mesh.material.SetFloat('metallic', .7) || !mesh.material.SetFloat('roughness', .2) ||
            !mesh.material.SetType('builtin/materials/default-pbr') || mesh.material.SetType('unknown') ||
            mesh.material.SetFloat('missing', 1) || mesh.material.SetFloat('baseColor', .2) ||
            mesh.material.SetV2('roughness', .1, .2) || mesh.material.SetV3('baseColor', .2, .3, .4) ||
            mesh.material.SetV4('baseColor', 2, .3, .4, 1) ||
            mesh.material.SetFloat('roughness', Infinity) ||
            !mesh.SetEnabled(false) || !mesh.SetEnabled(true) ||
            !cameraNode.GetComponent(Transform).SetPosition(0, 0, 8) || !camera.SetPerspective(45, .1, 200) ||
            !this.scene.SetActiveCamera(camera) || !light.SetColor(1, .9, .8) ||
            !light.SetIntensity(5) || !lightNode.GetComponent(Transform).SetRotation(-30, 20, 0)) return false;
        const parent = this.scene.CreateNode('temporary-parent');
        const child = this.scene.CreateNode('temporary-child');
        const component = child?.AddComponent(Light);
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
        if (!this.cube.GetComponent(Transform).SetRotation(0, this.angle, 0)) throw new Error('stale cube');
    }
    OnShutdown() {
        if (Time.deltaTime !== 0 || !this.scene.IsValid() || !this.cube.GetComponent(Transform).SetScale(1, 1, 1))
            throw new Error('scene must survive through OnShutdown');
        Debug.Log('scene:shutdown-valid');
    }
}
)js";

bool run_scene_cases(const fs::path& workspace)
{
    engine::WorldSystem world;
    engine::plugins::script::ScriptSystem script(&world);
    Fixture original;
    if (!expect(world.initialize({}) && script.initialize({}),
                "initialize native scene bindings") ||
        !fixture(workspace, "scene-first", {{"scripts/game.js", scene_source}}, original) ||
        !expect(apply(script, original) && world.scene() != nullptr &&
                    world.scene()->nodes().size() == 3,
                "scene, generic node components and typed materials execute"))
    {
        return false;
    }
    const auto* cube = world.scene()->nodes().front().get();
    float metallic = 0;
    if (!expect(cube->get_component<engine::MeshRenderer>()->material().get_float("metallic",
                                                                                  metallic) &&
                    metallic == .7F && world.scene()->active_camera() != nullptr &&
                    script.tick(.25) &&
                    cube->get_component<engine::Transform>()->settings().rotation.y() == 2.5,
                "component settings and Time.deltaTime reach the native scene"))
    {
        return false;
    }
    const auto old_id = world.scene()->id();
    Fixture bad;
    Fixture replacement;
    if (!fixture(workspace, "scene-bad", {{"scripts/game.js", "export default class { ??? }"}},
                 bad) ||
        !expect(!script.prepare_gameplay(bad.catalog, bad.mounts) && world.scene()->id() == old_id,
                "failed candidate preserves the exact old scene") ||
        !fixture(workspace, "scene-next", {{"scripts/game.js", scene_source}}, replacement) ||
        !expect(apply(script, replacement) && world.scene()->id() != old_id &&
                    log_contains("scene:shutdown-valid"),
                "old scene survives through its shutdown callback"))
    {
        return false;
    }
    Fixture partial;
    if (!fixture(
            workspace, "scene-partial",
            {{"scripts/game.js",
              "import {Gameplay,Scene,Debug} from 'engine';export default class extends Gameplay{"
              "s=null;OnInitialize(){this.s=Scene.Create('partial');return false;}"
              "OnShutdown(){if(this.s.IsValid())Debug.Log('partial-scene:shutdown');}}"}},
            partial) ||
        !expect(!apply(script, partial) && world.scene() == nullptr &&
                    log_contains("partial-scene:shutdown"),
                "partial initialization releases its native scene after shutdown"))
    {
        return false;
    }
    const std::string stale = R"js(
import {Gameplay,Scene,Debug,MeshRenderer,Transform} from 'engine';
export default class extends Gameplay {
    scene=null; node=null; mesh=null; transform=null; material=null;
    OnInitialize(){this.scene=Scene.Create('external-test'); this.node=this.scene.CreateNode('owned');
        this.mesh=this.node.AddComponent(MeshRenderer); this.transform=this.node.GetComponent(Transform); this.material=this.mesh.material; return this.mesh!==null;}
    Update(){if(this.scene.IsValid() || this.node.IsValid() || this.mesh.IsValid() ||
        this.transform.SetPosition(9,9,9) || this.node.GetComponent(MeshRenderer)!==null || this.material.IsValid() ||
        this.material.SetV4('baseColor',1,1,1,1) ||
        this.material.SetTexture('baseColorTexture','builtin/textures/white-srgb') ||
        this.material.GetTexture('baseColorTexture')!==null || this.mesh.GetMaterial(0)!==null ||
        this.mesh.SetMaterialCount(2) || this.mesh.SetEnabled(false) || this.scene.Destroy() ||
        Scene.Create('steal')!==null) throw new Error('stale references were accepted');
        Debug.Log('stale:rejected');}
}
)js";
    Fixture external;
    if (!fixture(workspace, "scene-external", {{"scripts/game.js", stale}}, external) ||
        !expect(apply(script, external), "start stale wrapper fixture"))
    {
        return false;
    }
    world.destroy_scene();
    auto* foreign = world.create_scene("foreign");
    if (!expect(foreign != nullptr && script.tick(.01) && log_contains("stale:rejected"),
                "stale wrappers cannot access a replacement scene"))
    {
        return false;
    }
    script.stop_gameplay();
    if (!expect(world.scene() == foreign, "script cleanup leaves a foreign scene intact"))
    {
        return false;
    }
    world.destroy_scene();
    script.shutdown();
    world.shutdown();
    return true;
}

bool run_guid_cases(const fs::path& workspace)
{
    engine::WorldSystem world;
    engine::plugins::script::ScriptSystem script(&world);
    if (!world.initialize({}) || !script.initialize({}))
    {
        return false;
    }
    auto* scene = world.create_scene("guid");
    if (!scene || !scene->create_node("GUID Node"))
    {
        return false;
    }
    engine::schema::SceneDataT data;
    if (!scene->build_data(data))
    {
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    engine::schema::FinishSceneDataBuffer(builder, engine::schema::SceneData::Pack(builder, &data));
    const std::string bytes(reinterpret_cast<const char*>(builder.GetBufferPointer()),
                            builder.GetSize());
    world.destroy_scene();
    const std::string source = R"js(
import {Gameplay,Scene,Debug} from 'engine';
import {value} from './lib/value.js';
export default class extends Gameplay {
    scene=null;
    OnInitialize(){this.scene=Scene.Load('project/scenes/main.scene');
        if(!this.scene || !this.scene.FindNode('GUID Node') || Scene.Load('project/scenes/missing.scene')!==null)return false;
        Debug.Log('guid:start:'+value);return true;}
    Update(){if(!this.scene.IsValid())throw new Error('scene expired');Debug.Log('guid:update:'+value);}
}
)js";
    Fixture version;
    if (!fixture(workspace, "guid-first",
                 {{"scripts/game.js", source}, {"scripts/lib/value.js", "export const value=7;"}},
                 version, bytes) ||
        !expect(apply(script, version) && log_contains("guid:start:7"),
                "GUID scripts and Scene.Load execute") ||
        !write_file(version.root / "gen" / guid_for("project/scenes/main.scene"), "corrupt") ||
        !expect(script.tick(.01) && log_contains("guid:update:7"),
                "loaded scene owns its data independently"))
    {
        return false;
    }
    Fixture missing;
    if (!fixture(workspace, "guid-missing", {{"scripts/game.js", source}}, missing, bytes) ||
        !expect(!script.prepare_gameplay(missing.catalog, missing.mounts) &&
                    script.status().active_revision == "guid-first" && world.scene() != nullptr,
                "missing GUID module never falls back to another directory"))
    {
        return false;
    }
    script.stop_gameplay();
    if (!expect(world.scene() == nullptr, "scene belongs to its gameplay run"))
    {
        return false;
    }
    script.shutdown();
    world.shutdown();
    return true;
}

bool run_physics_audio_binding_cases(const fs::path& workspace)
{
    constexpr std::string_view source = R"js(
import { Gameplay, Scene, Component, Collider, RigidBody, CharacterMotor, AudioSource, AudioListener } from 'engine';
export default class extends Gameplay {
    OnInitialize() {
        const scene = Scene.Create('physics-audio-bindings');
        const node = scene?.CreateNode('emitter');
        const listenerNode = scene?.CreateNode('listener');
        if (!scene || !node || !listenerNode) return false;
        const collider = node.AddComponent(Collider);
        const body = node.AddComponent(RigidBody);
        const audio = node.AddComponent(AudioSource);
        const listener = listenerNode.AddComponent(AudioListener);
        const motor = listenerNode.AddComponent(CharacterMotor);
        if (!motor || !(motor instanceof Component) ||
            !(listenerNode.GetComponent(CharacterMotor) instanceof CharacterMotor) ||
            motor.SetCapsule(-1, .5) || motor.SetSlopeLimit(90) || motor.SetStepHeight(-1) ||
            motor.SetGroundSnapDistance(Infinity) || motor.SetGravityFactor(-1) ||
            motor.SetExternalDamping(-1) || motor.SetExternalDamping(1001) ||
            motor.SetExternalDamping(NaN) || motor.SetExternalDamping(Infinity) ||
            motor.SetExternalDamping('6') || motor.AddVelocity(0, NaN, 0) ||
            motor.AddVelocity(Infinity, 0, 0) || motor.AddVelocity(0, 0, 10001) ||
            motor.AddVelocity(0, 1) || motor.AddVelocity('1', 0, 0) ||
            motor.SetVelocity(NaN, 0) || motor.Jump(5) || motor.grounded ||
            !motor.SetCapsule(.4, .7) || !motor.SetSlopeLimit(45) || !motor.SetStepHeight(.25) ||
            !motor.SetGroundSnapDistance(.15) || !motor.SetGravityFactor(1) ||
            !motor.SetExternalDamping(3.5) || !motor.SetVelocity(2, -3) ||
            !motor.AddVelocity(2, 1, -3) || !motor.ClearInput() ||
            motor.AddVelocity(9999, 0, 0) || motor.GetVelocity()?.y !== 0 ||
            !motor.SetEnabled(false) || motor.AddVelocity(0, 1, 0) ||
            !motor.ClearInput() || !motor.SetEnabled(true)) return false;
        if (!collider || !body || !audio || !listener || !(collider instanceof Component) ||
            !(node.GetComponent(RigidBody) instanceof RigidBody) ||
            !(node.GetComponent(AudioSource) instanceof AudioSource) ||
            !(listenerNode.GetComponent(AudioListener) instanceof AudioListener) ||
            node.AddComponent(Collider) !== null) return false;
        if (collider.SetBox(0, 1, 1) || collider.SetSphere(NaN) ||
            collider.SetCapsule(-1, 1) || collider.SetTrigger('yes') ||
            !collider.SetSphere(.4) || !collider.SetCapsule(.25, .5) ||
            !collider.SetBox(.5, 1, .5) || !collider.SetFriction(.6) ||
            !collider.SetRestitution(.2) || !collider.SetTrigger(false)) return false;
        if (body.SetMass(0) || body.SetMotion('flying') || body.SetVelocity(Infinity, 0, 0) ||
            !body.SetMass(2) || !body.SetVelocity(1, 2, 3) || body.GetVelocity()?.y !== 2 ||
            body.SetMotion('static') || !body.SetVelocity(0, 0, 0) ||
            !body.SetMotion('static') || body.AddImpulse(1, 0, 0) ||
            !body.SetMotion('kinematic') || !body.SetMotion('dynamic') ||
            !body.AddImpulse(0, 1, 0) || !body.SetVelocity(1, 2, 3)) return false;
        if (audio.Play() || audio.SetVolume(-1) || audio.SetPitch(0) ||
            audio.SetLoop(1) || audio.SetSpatial('false') || audio.SetDistances(10, 1) ||
            listener.SetVolume(NaN) || !listener.SetVolume(.75) ||
            !audio.SetVolume(.5) || !audio.SetPitch(1.25) || !audio.SetLoop(true) ||
            !audio.SetSpatial(false) || !audio.SetPlayOnAwake(false) ||
            !audio.SetDistances(1, 25) || !audio.SetClip('project/sounds/test.wav') ||
            audio.SetClip('builtin/meshes/cube') || !audio.Play() ||
            audio.isPlaying || audio.playback !== 'pending' || !audio.Pause() ||
            audio.playback !== 'paused' || !audio.Play() || !audio.Stop() ||
            audio.playback !== 'stopped' || audio.isPlaying) return false;
        const temporary = scene.CreateNode('temporary');
        const staleCollider = temporary?.AddComponent(Collider);
        const staleBody = temporary?.AddComponent(RigidBody);
        const staleAudio = temporary?.AddComponent(AudioSource);
        const staleListener = temporary?.AddComponent(AudioListener);
        const staleMotor = temporary?.AddComponent(CharacterMotor);
        if (!staleCollider || !staleBody || !staleAudio || !staleListener || !staleMotor || !temporary.Destroy() ||
            staleMotor.GetVelocity() !== null || staleMotor.grounded || staleMotor.SetVelocity(1, 0) ||
            staleMotor.AddVelocity(1, 2, 3) || staleMotor.ClearInput() ||
            staleMotor.SetExternalDamping(6) ||
            staleCollider.SetSphere(1) || staleBody.GetVelocity() !== null ||
            staleAudio.Play() || staleAudio.playback !== null || staleAudio.isPlaying ||
            staleListener.SetVolume(1)) return false;
        return true;
    }
}
)js";
    engine::schema::AudioClipDataT clip;
    clip.frames = 16;
    clip.samples.assign(32, 0.25F);
    flatbuffers::FlatBufferBuilder builder;
    engine::schema::FinishAudioClipDataBuffer(builder,
                                              engine::schema::AudioClipData::Pack(builder, &clip));
    const std::string_view audio(reinterpret_cast<const char*>(builder.GetBufferPointer()),
                                 builder.GetSize());
    engine::WorldSystem world;
    engine::plugins::script::ScriptSystem script(&world);
    Fixture input;
    if (!expect(world.initialize({}) && script.initialize({}),
                "initialize physics and audio bindings") ||
        !fixture(workspace, "physics-audio-bindings", {{"scripts/game.js", std::string(source)}},
                 input, {}, audio) ||
        !expect(apply(script, input), "typed physics/audio bindings, validation and stale handles"))
    {
        return false;
    }
    const auto* scene = world.scene();
    if (!expect(scene != nullptr && scene->nodes().size() == 2, "binding scene retains two nodes"))
    {
        return false;
    }
    const auto* emitter = scene->nodes().front().get();
    const auto* collider = emitter->get_component<engine::Collider>();
    const auto* body = emitter->get_component<engine::RigidBody>();
    const auto* source_component = emitter->get_component<engine::AudioSource>();
    const auto* listener = scene->nodes().back()->get_component<engine::AudioListener>();
    const auto* motor = scene->nodes().back()->get_component<engine::CharacterMotor>();
    if (!expect(collider != nullptr && body != nullptr && source_component != nullptr &&
                    listener != nullptr && motor != nullptr && motor->settings().radius == .4F &&
                    motor->settings().external_damping == 3.5F &&
                    collider->settings().shape == engine::schema::ColliderShape::box &&
                    body->settings().mass == 2 && body->linear_velocity().z() == 3 &&
                    source_component->clip_data() != nullptr && source_component->settings().loop &&
                    source_component->settings().volume == .5F &&
                    listener->settings().volume == .75F,
                "script settings reach native physics and audio components"))
    {
        return false;
    }
    script.stop_gameplay();
    const bool cleared = world.scene() == nullptr;
    script.shutdown();
    world.shutdown();
    return expect(cleared, "script shutdown releases component resource references with its scene");
}

bool run_frame_rate_binding_cases(const fs::path& workspace)
{
    auto& engine = engine::Engine::instance();
    const engine::EngineConfig config;
    engine::plugins::script::ScriptSystem script;
    constexpr std::string_view source = R"js(
import { Gameplay, Engine } from 'engine';
const topLevelBlocked = !Engine.SetTargetFrameRate(120) && Engine.GetTargetFrameRate() === null;
export default class extends Gameplay {
    constructor() {
        super();
        this.constructorBlocked = !Engine.SetTargetFrameRate(90);
    }
    OnInitialize() {
        return topLevelBlocked && this.constructorBlocked && Object.isFrozen(Engine) &&
            Engine.SetTargetFrameRate(-1) && Engine.GetTargetFrameRate() === -1 &&
            Engine.SetTargetFrameRate(45) && Engine.GetTargetFrameRate() === 45 &&
            !Engine.SetTargetFrameRate(0) && !Engine.SetTargetFrameRate(-2) &&
            !Engine.SetTargetFrameRate(45.5) && !Engine.SetTargetFrameRate('45') &&
            !Engine.SetTargetFrameRate(true) && !Engine.SetTargetFrameRate(NaN) &&
            !Engine.SetTargetFrameRate(Infinity) && !Engine.SetTargetFrameRate(2147483648) &&
            Engine.GetTargetFrameRate() === 45;
    }
    Update() {
        if (!Engine.SetTargetFrameRate(30)) throw new Error('Cannot change frame rate');
    }
    OnShutdown() {
        if (!Engine.SetTargetFrameRate(-1)) throw new Error('Cannot release frame rate cap');
    }
}
)js";
    Fixture input;
    if (!engine.set_target_frame_rate(60) || !script.initialize(config) ||
        !fixture(workspace, "frame-rate", {{"scripts/game.js", std::string(source)}}, input) ||
        !expect(script.prepare_gameplay(input.catalog, input.mounts) &&
                    engine.target_frame_rate() == 60,
                "candidate module and constructor cannot change the frame rate") ||
        !engine::ResourceSystem::instance()->update_catalog(input.catalog.data(), input.mounts) ||
        !expect(script.start_prepared_gameplay() && engine.target_frame_rate() == 45,
                "gameplay sets the frame rate; invalid typed inputs preserve it") ||
        !expect(script.tick(.016) && script.status().running && engine.target_frame_rate() == 30,
                "gameplay can adjust the active cap during update") ||
        !expect(script.prepare_gameplay(input.catalog, input.mounts) &&
                    engine.target_frame_rate() == 30,
                "hot reload candidate leaves the active run's frame rate unchanged"))
    {
        return false;
    }
    script.stop_gameplay();
    return expect(engine.target_frame_rate() == -1 && script.status().last_error.empty(),
                  "shutdown callback can remove the cap before the host restores its own value");
}

bool run_render_binding_cases(const fs::path& workspace)
{
    const engine::EngineConfig config;
    Fixture missing;
    constexpr std::string_view missing_source = R"js(
import { Gameplay, Render } from 'engine';
export default class extends Gameplay {
    OnInitialize() {
        return !Render.SetAmbient(1, 1, 1, 1) && !Render.SetExposure(1) &&
            !Render.SetFilmicTonemap(true) && !Render.SetDirectionalShadows(true) &&
            !Render.SetAmbientOcclusion(true) &&
            !Render.SetHdr(true) && !Render.SetBloom(true) &&
            !Render.SetShadowCascades(2, 120) &&
            !Render.SetClearColor(0xffffffff);
    }
}
)js";
    {
        engine::plugins::script::ScriptSystem script;
        if (!script.initialize(config) ||
            !fixture(workspace, "render-without-service",
                     {{"scripts/game.js", std::string(missing_source)}}, missing) ||
            !expect(apply(script, missing), "render binding rejects an absent injected service"))
        {
            return false;
        }
    }

    // 设置接口不要求初始化图形设备，本测试不创建窗口或依赖 GPU。
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows);
    engine::plugins::script::ScriptSystem script(nullptr, &renderer);
    constexpr std::string_view source = R"js(
import { Gameplay, Render } from 'engine';
const topLevelBlocked = !Render.SetExposure(9) && !Render.SetAmbient(1, 1, 1, 5);
export default class extends Gameplay {
    constructor() { super(); this.constructorBlocked = !Render.SetExposure(8); }
    OnInitialize() {
        return topLevelBlocked && this.constructorBlocked && Object.isFrozen(Render) &&
            !Render.SetBloom(true) && Render.SetHdr(true) && Render.SetBloom(true, .8, .35, 2) &&
            !Render.SetHdr(false) &&
            Render.SetAmbient(.2, .3, .4, .15) && Render.SetExposure(1.4) &&
            Render.SetAmbientTricolor([.5,.6,.7], [.3,.4,.5], [.1,.2,.3], .15) &&
            Render.SetLinearFog(true, .2, .3, .4, 60, 300) &&
            Render.SetExponentialFog(true, .2, .3, .4, .007, true) &&
            !Render.SetExponentialFog(true, .8, .7, .6, -.01, true) &&
            !Render.SetExponentialFog(true, .8, .7, .6, Infinity, true) &&
            !Render.SetExponentialFog(true, .8, .7, .6, .01, 1) &&
            !Render.SetAmbientTricolor(null, [.3,.4,.5], [.1,.2,.3], .2) &&
            !Render.SetAmbientTricolor([1,2,3], [.3,.4,.5], [.1,.2,.3], .2) &&
            !Render.SetLinearFog(true, .8, .7, .6, 300, 60) &&
            !Render.SetLinearFog(true, .8, .7, .6, -1, 60) &&
            Render.SetFilmicTonemap(true) && Render.SetDirectionalShadows(true) &&
            Render.SetAmbientOcclusion(true, 1.4, .8, 2, 40, 140) &&
            Render.SetShadowCascades(2, 120, .7, .2) &&
            Render.SetDirectionalShadows(true, 1024, .002) &&
            Render.SetClearColor(0xffffffff) && Render.SetClearColor(0x56789aff) &&
            !Render.SetAmbient(.8, .7, -1, .9) && !Render.SetAmbient(.8, .7, .6, -1) &&
            !Render.SetAmbient(.8, .7, .6, 1001) && !Render.SetAmbient(NaN, 0, 0, 1) &&
            !Render.SetAmbient('1', 0, 0, 1) && !Render.SetExposure(0) &&
            !Render.SetExposure(Infinity) && !Render.SetExposure(101) &&
            !Render.SetFilmicTonemap(1) && !Render.SetDirectionalShadows(1) &&
            !Render.SetDirectionalShadows(false, 65536, .02) &&
            !Render.SetDirectionalShadows(false, 1000, .03) &&
            !Render.SetDirectionalShadows(false, 1024.5, .03) &&
            !Render.SetDirectionalShadows(false, 128, .2) &&
            !Render.SetShadowCascades(3, 120) && !Render.SetShadowCascades(2.5, 120) &&
            !Render.SetShadowCascades('2', 120) && !Render.SetShadowCascades(2, 0) &&
            !Render.SetShadowCascades(2, 100001) && !Render.SetShadowCascades(2, NaN) &&
            !Render.SetShadowCascades(2, 120, 1.1) &&
            !Render.SetShadowCascades(2, 120, .5, .31) &&
            !Render.SetAmbientOcclusion(1) && !Render.SetAmbientOcclusion(true, 0) &&
            !Render.SetAmbientOcclusion(true, 101) &&
            !Render.SetAmbientOcclusion(true, 1, 2.1) &&
            !Render.SetAmbientOcclusion(true, 1, 1, NaN) &&
            !Render.SetAmbientOcclusion(true, 1, 1, .09) &&
            !Render.SetAmbientOcclusion(true, 1, 1, 8.1) &&
            !Render.SetAmbientOcclusion(true, 1, 1, 1.5, 150, 150) &&
            !Render.SetAmbientOcclusion(true, 1, 1, 1.5, 80, 100001) &&
            !Render.SetHdr(1) && !Render.SetBloom(1) && !Render.SetBloom(true, -1) &&
            !Render.SetBloom(true, 101) && !Render.SetBloom(true, 1, 10.1) &&
            !Render.SetBloom(true, 1, NaN) && !Render.SetBloom(true, 1, .2, .24) &&
            !Render.SetBloom(true, 1, .2, 4.1) &&
            !Render.SetClearColor(-1) && !Render.SetClearColor(0x100000000) &&
            !Render.SetClearColor(.5) && !Render.SetClearColor('0x123456ff');
    }
    Update() {
        if (!Render.SetExposure(.75) || !Render.SetDirectionalShadows(false))
            throw new Error('Cannot set render options during Update');
    }
    OnShutdown() {
        if (!Render.SetExposure(.5) || Render.SetHdr(false) ||
            !Render.SetBloom(false, .8, .35, 2) || !Render.SetHdr(false))
            throw new Error('Renderer must outlive gameplay callbacks and preserve flag order');
    }
}
)js";
    Fixture input;
    if (!script.initialize(config) ||
        !fixture(workspace, "render-settings", {{"scripts/game.js", std::string(source)}}, input) ||
        !expect(apply(script, input), "render settings apply in OnInitialize with typed inputs"))
    {
        return false;
    }
    const auto settings = renderer.settings();
    if (!expect(settings.ambient_color == std::array<float, 3>{.2F, .3F, .4F} &&
                    settings.ambient_intensity == .15F && settings.exposure == 1.4F &&
                    settings.ambient_tricolor &&
                    settings.ambient_sky_color == std::array<float, 3>{.5F, .6F, .7F} &&
                    settings.fog_mode == engine::FogMode::exponential_squared &&
                    settings.fog_density == .007F && settings.fog_start == 60 &&
                    settings.fog_end == 300 &&
                    settings.fog_color == std::array<float, 3>{.2F, .3F, .4F} &&
                    settings.filmic_tonemap && settings.directional_shadows &&
                    settings.ao_enabled && settings.ao_radius == 1.4F &&
                    settings.ao_strength == .8F && settings.ao_power == 2.0F &&
                    settings.ao_fade_start == 40 && settings.ao_fade_end == 140 &&
                    settings.hdr_enabled && settings.bloom_enabled &&
                    settings.bloom_threshold == .8F && settings.bloom_intensity == .35F &&
                    settings.bloom_radius == 2 &&
                    settings.shadow_resolution == 1024 && settings.shadow_bias == .002F &&
                    settings.shadow_cascades == 2 && settings.shadow_distance == 120.0F &&
                    settings.shadow_split_lambda == .7F && settings.shadow_cascade_blend == .2F &&
                    settings.clear_color == 0x56789aff,
                "invalid calls preserve all fields, including partial ambient and shadow input") ||
        !expect(script.tick(.016) && script.status().running &&
                    renderer.settings().exposure == .75F &&
                    renderer.settings().ao_enabled && renderer.settings().ao_radius == 1.4F &&
                    renderer.settings().ao_strength == .8F && renderer.settings().ao_power == 2 &&
                    renderer.settings().ao_fade_start == 40 &&
                    renderer.settings().ao_fade_end == 140 &&
                    renderer.settings().hdr_enabled && renderer.settings().bloom_enabled &&
                    renderer.settings().bloom_threshold == .8F &&
                    renderer.settings().bloom_intensity == .35F &&
                    renderer.settings().bloom_radius == 2 &&
                    !renderer.settings().directional_shadows &&
                    renderer.settings().shadow_resolution == 2048 &&
                    renderer.settings().shadow_bias == .001F &&
                    renderer.settings().shadow_cascades == 2 &&
                    renderer.settings().shadow_distance == 120.0F &&
                    renderer.settings().shadow_split_lambda == .7F &&
                    renderer.settings().shadow_cascade_blend == .2F,
                "Update can change renderer settings and shadow defaults match the native API"))
    {
        return false;
    }
    script.stop_gameplay();
    if (!expect(renderer.settings().exposure == .5F && !script.status().running &&
                    !renderer.settings().hdr_enabled && !renderer.settings().bloom_enabled &&
                    renderer.settings().bloom_intensity == .35F,
                "shutdown callback runs while the injected renderer remains valid") ||
        !renderer.set_settings({}) ||
        !expect(script.prepare_gameplay(input.catalog, input.mounts) &&
                    renderer.settings().exposure == 1.0F &&
                    renderer.settings().ambient_intensity == 0.0F &&
                    !renderer.settings().directional_shadows,
                "preparing a candidate does not apply project render settings") ||
        !expect(script.start_prepared_gameplay() && renderer.settings().exposure == 1.4F &&
                    renderer.settings().ambient_intensity == .15F &&
                    renderer.settings().directional_shadows,
                "starting the project again reapplies its own render settings"))
    {
        return false;
    }
    script.stop_gameplay();
    return expect(renderer.settings().exposure == .5F && script.status().last_error.empty(),
                  "repeated lifecycle preserves the renderer borrowing contract");
}

bool run_scene_render_binding_cases(const fs::path& workspace)
{
    engine::WorldSystem world;
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows, &world);
    engine::plugins::script::ScriptSystem script(&world, &renderer);
    constexpr std::string_view source = R"js(
import { Gameplay, Scene, Camera, Light, Render,
         EnvironmentComponent, PostProcessComponent } from 'engine';
export default class extends Gameplay {
    frame = 0;
    OnInitialize() {
        this.scene = Scene.Create('Scene environment');
        if (!this.scene || Render.SetExposure(2) || Render.SetAmbient(1, 1, 1, 2) ||
            Render.SetShadowCascades(2, 120) || Render.SetAmbientOcclusion(true) ||
            Render.SetHdr(true) || Render.SetBloom(true)) return false;
        const environmentNode = this.scene.CreateNode('Environment');
        this.environment = environmentNode.AddComponent(EnvironmentComponent);
        const cameraNode = this.scene.CreateNode('Camera');
        const camera = cameraNode.AddComponent(Camera);
        this.post = cameraNode.AddComponent(PostProcessComponent);
        const light = this.scene.CreateNode('Sun').AddComponent(Light);
        this.light = light;
        if (!this.environment || !camera || !this.post || !light ||
            !this.scene.SetActiveCamera(camera)) return false;
        return environmentNode.GetComponent(EnvironmentComponent).IsValid() &&
            !environmentNode.AddComponent(EnvironmentComponent) &&
            this.environment.SetIbl(true, .5) &&
            !this.environment.SetIbl(true, -1) && !this.environment.SetIbl(1, .5) &&
            this.environment.SetAmbient(.2, .3, .4, .5) &&
            this.environment.SetAmbientTricolor([.6,.7,.8], [.3,.4,.5], [.1,.2,.3], .6) &&
            this.environment.SetLinearFog(true, .4, .5, .6, 2, 90) &&
            this.environment.SetExponentialFog(true, .3, .4, .5, .03, true) &&
            this.environment.SetClearColor(0x123456ff) &&
            !this.environment.SetAmbient(.8, .7, -1, .9) &&
            !this.environment.SetAmbientTricolor([1,2,3], [1,1,1], [1,1,1], .2) &&
            !this.environment.SetLinearFog(true, .4, .5, .6, 90, 2) &&
            !this.environment.SetExponentialFog(true, .4, .5, .6, -.1) &&
            !this.environment.SetClearColor(-1) &&
            !this.post.SetBloom(true) && this.post.SetHdr(true) &&
            this.post.SetBloom(true, 1.2, .3, 2) && !this.post.SetHdr(false) &&
            !this.post.SetHdr(1) && !this.post.SetBloom(1) &&
            !this.post.SetBloom(true, -1) && !this.post.SetBloom(true, NaN) &&
            !this.post.SetBloom(true, 1, 10.1) && !this.post.SetBloom(true, 1, .2, 4.1) &&
            this.post.SetAmbientOcclusion(true, 2, 1.2, 3, 20, 100) &&
            this.post.SetExposure(1.7) && this.post.SetFilmicTonemap(true) &&
            !this.post.SetAmbientOcclusion(1) && !this.post.SetAmbientOcclusion(true, 0) &&
            !this.post.SetAmbientOcclusion(true, 101) &&
            !this.post.SetAmbientOcclusion(true, 1, -.1) &&
            !this.post.SetAmbientOcclusion(true, 1, 1, Infinity) &&
            !this.post.SetAmbientOcclusion(true, 1, 1, .09) &&
            !this.post.SetAmbientOcclusion(true, 1, 1, 1.5, -1, 100) &&
            !this.post.SetAmbientOcclusion(true, 1, 1, 1.5, 100, 80) &&
            !this.post.SetExposure(0) && !this.post.SetFilmicTonemap(1) &&
            light.SetDirectional() && Render.SetShadowCascades(1, 100) &&
            light.SetShadowCascades(2, 90, .8, .15) && light.SetShadows(true, 1024, .002) &&
            !light.SetShadowCascades(3, 90) && !light.SetShadowCascades(2.5, 90) &&
            !light.SetShadowCascades(2, 0) && !light.SetShadowCascades(2, Infinity) &&
            !light.SetShadowCascades(2, 100001) && !light.SetShadowCascades(2, 90, NaN) &&
            !light.SetShadowCascades(2, 90, -1) && !light.SetShadowCascades(2, 90, .5, .31) &&
            !light.SetShadows(true, 1000, .01) && !light.SetShadows(true, 1024, .3);
    }
    Update() {
        if (++this.frame === 1) {
            if (!this.environment.SetAmbient(.1, .2, .3, .4) || !this.post.SetExposure(.8))
                throw new Error('Cannot update scene component parameters');
        } else {
            if (!this.scene.Destroy() || this.environment.SetClearColor(0xffffffff) ||
                this.post.SetExposure(1) || this.post.SetAmbientOcclusion(true) ||
                this.post.SetHdr(true) || this.post.SetBloom(true) ||
                this.light.SetShadowCascades(4, 150))
                throw new Error('Stale component handle was accepted');
            this.scene = Scene.Create('Fresh scene');
            if (!this.scene) throw new Error('Cannot create replacement scene');
        }
    }
}
)js";
    Fixture input;
    if (!world.initialize({}) || !script.initialize({}) ||
        !fixture(workspace, "scene-render-components", {{"scripts/game.js", std::string(source)}},
                 input) ||
        !expect(apply(script, input), "new scene render component factories and typed setters"))
    {
        return false;
    }
    const auto settings = renderer.settings();
    if (!expect(settings.ibl_enabled && settings.ibl_intensity == .5F &&
                    settings.ambient_tricolor && settings.ambient_intensity == .6F &&
                    settings.ambient_sky_color == std::array<float, 3>{.6F, .7F, .8F} &&
                    settings.fog_mode == engine::FogMode::exponential_squared &&
                    settings.fog_density == .03F && settings.fog_start == 2 &&
                    settings.fog_end == 90 && settings.clear_color == 0x123456ff &&
                    settings.exposure == 1.7F && settings.filmic_tonemap &&
                    settings.ao_enabled && settings.ao_radius == 2 &&
                    settings.ao_strength == 1.2F && settings.ao_power == 3 &&
                    settings.ao_fade_start == 20 && settings.ao_fade_end == 100 &&
                    settings.hdr_enabled && settings.bloom_enabled &&
                    settings.bloom_threshold == 1.2F && settings.bloom_intensity == .3F &&
                    settings.bloom_radius == 2 &&
                    settings.directional_shadows && settings.shadow_resolution == 1024 &&
                    settings.shadow_bias == .002F && settings.shadow_cascades == 2 &&
                    settings.shadow_distance == 90.0F && settings.shadow_split_lambda == .8F &&
                    settings.shadow_cascade_blend == .15F,
                "renderer reads component state; invalid setters preserve prior fields") ||
        !expect(script.tick(.016) && script.status().running &&
                    renderer.settings().ambient_intensity == .4F &&
                    !renderer.settings().ambient_tricolor && renderer.settings().exposure == .8F &&
                    renderer.settings().ao_enabled && renderer.settings().ao_radius == 2 &&
                    renderer.settings().ao_strength == 1.2F && renderer.settings().ao_power == 3 &&
                    renderer.settings().ao_fade_start == 20 &&
                    renderer.settings().ao_fade_end == 100 &&
                    renderer.settings().hdr_enabled && renderer.settings().bloom_enabled &&
                    renderer.settings().bloom_threshold == 1.2F &&
                    renderer.settings().bloom_intensity == .3F &&
                    renderer.settings().bloom_radius == 2,
                "live component changes affect the current scene") ||
        !expect(script.tick(.016) && script.status().running &&
                    renderer.settings().ambient_intensity == 0.0F &&
                    renderer.settings().exposure == 1.0F &&
                    !renderer.settings().ao_enabled && renderer.settings().ao_radius == 1 &&
                    renderer.settings().ao_strength == 1 && renderer.settings().ao_power == 1.5F &&
                    renderer.settings().ao_fade_start == 80 &&
                    renderer.settings().ao_fade_end == 150 &&
                    !renderer.settings().hdr_enabled && !renderer.settings().bloom_enabled &&
                    renderer.settings().bloom_threshold == 1 &&
                    renderer.settings().bloom_intensity == .15F &&
                    renderer.settings().bloom_radius == 1 &&
                    !renderer.settings().directional_shadows,
                "new scene does not inherit old environment or camera parameters"))
    {
        return false;
    }
    script.stop_gameplay();
    return expect(script.status().last_error.empty(), "scene render bindings shut down cleanly");
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
    engine::ResourceSystem resources;
    const bool passed = resources.initialize(config) && run_cases(workspace) &&
                        run_module_contract_cases(workspace) && run_job_contract_cases(workspace) &&
                        run_native_boundary_cases(workspace) && run_scene_cases(workspace) &&
                        run_guid_cases(workspace) && run_physics_audio_binding_cases(workspace) &&
                        run_render_binding_cases(workspace) &&
                        run_scene_render_binding_cases(workspace) &&
                        run_frame_rate_binding_cases(workspace);
    resources.shutdown();
    engine::LogSystem::instance().shutdown();
    engine::App::shutdown();
    if (passed)
    {
        std::puts("Script runtime PASS: ES modules, scenes, components, snapshots, hot reload and "
                  "failures");
    }
    return passed ? 0 : 1;
}
