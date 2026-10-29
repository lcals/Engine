#include "runtime/app/app.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/script/script_system.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/world/world_system.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
#include <cstdint>
#include <memory>
#include <utility>

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
             std::string_view scene_bytes = {})
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
    return expect(output.catalog.assign(data), "assign runtime fixture metadata");
}

bool apply(engine::ScriptSystem& script, const Fixture& candidate,
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
    std::string text;
    return engine::LogSystem::instance().flush() &&
           engine::ResourceSystem::read_text(engine::LogSystem::instance().file_path(), text) &&
           text.find(message) != std::string::npos;
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
    engine::ScriptSystem script;
    Fixture current;
    if (!expect(script.initialize({}), "initialize V8") ||
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
            !engine::ResourceSystem::instance()->update_catalog(next.catalog.data(), next.mounts),
            "running scripts retain their resources and reject conflicting updates"))
    {
        return false;
    }
    for (const auto& [name, source] : std::map<std::string, std::string>{
             {"syntax", "export default class { ??? }"},
             {"missing", "import './missing.js'; export default class {}"},
             {"escape", "import '../../outside.js'; export default class {}"},
             {"exports", "import {missing} from './lib/value.js'; export default class {}"},
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
                "isolate recreates while V8 process platform stays alive"))
    {
        return false;
    }
    script.stop_gameplay();
    script.stop_gameplay();
    script.shutdown();
    return true;
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
    engine::ScriptSystem script(&world);
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
    engine::ScriptSystem script(&world);
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
                        run_scene_cases(workspace) && run_guid_cases(workspace);
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
