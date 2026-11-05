#include "plugins/runtime/script/script_system.hpp"
#include "scene_snapshot.hpp"
#if defined(ENGINE_TEST_EFFECTS_PLUGIN)
#include "plugins/runtime/effects/effect_component.hpp"
#endif
#if defined(ENGINE_TEST_RESOURCE_GRAPH)
#include "plugins/runtime/resource_graph/resource_graph_system.hpp"
#endif
#include "runtime/app/app.hpp"
#include "runtime/audio/audio_system.hpp"
#include "runtime/movie/movie_player.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"
#include "runtime/engine.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/input/input_system.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/physics/physics_system.hpp"
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

#include <SDL3/SDL.h>
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
#include <thread>
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
             std::string_view scene_bytes = {}, std::string_view audio_bytes = {},
             const std::map<std::string, std::string>& json_resources = {},
             std::string_view movie_bytes = {})
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
        if (!add(module, module, engine::schema::AssetKind::script, source))
        {
            return false;
        }
    }
    if (!scene_bytes.empty() &&
        !add("scenes/main.scene", "", engine::schema::AssetKind::scene, scene_bytes))
    {
        return false;
    }
    if (!audio_bytes.empty() &&
        !add("sounds/test.wav", "", engine::schema::AssetKind::audio, audio_bytes))
    {
        return false;
    }
    if (!movie_bytes.empty() &&
        !add("movies/test.mp4", "", engine::schema::AssetKind::movie, movie_bytes))
    {
        return false;
    }
    for (const auto& [path, source] : json_resources)
    {
        if (!add(path, "", engine::schema::AssetKind::json, source))
        {
            return false;
        }
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

bool run_prepared_catalog_cases(const fs::path& workspace)
{
    engine::plugins::script::ScriptSystem script;
    Fixture first;
    Fixture second;
    const std::string source =
        "import {Gameplay} from 'engine'; export default class extends Gameplay {}";
    if (!script.initialize({}) ||
        !fixture(workspace, "prepared-first", {{"scripts/game.js", source}}, first) ||
        !fixture(workspace, "prepared-second", {{"scripts/game.js", source + "\n// next"}},
                 second) ||
        !apply(script, first))
    {
        return false;
    }
    const auto original_run = script.status().run_id;
    if (!expect(!script.prepare_gameplay(second.catalog, {}) && script.status().running &&
                    script.status().run_id == original_run,
                "candidate preparation requires its mounts and does not fall back to active "
                "resources") ||
        !expect(script.prepare_gameplay(second.catalog, second.mounts) &&
                    !script.start_prepared_gameplay() && !script.status().running &&
                    script.status().last_error ==
                        "Active script resources differ from the prepared catalog",
                "starting an unpublished candidate rejects mismatched active script content") ||
        !expect(apply(script, second) && script.status().running &&
                    script.status().active_revision == "prepared-second" &&
                    script.status().run_id != original_run,
                "publishing a prechecked candidate starts a fresh Run through the formal path"))
    {
        return false;
    }
    script.stop_gameplay();
    return true;
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
                    !script.prepare_gameplay(next.catalog, next.mounts, "/absolute.js"),
                "unsafe entry paths are rejected") ||
        !expect(apply(script, next) && script.tick(.01) && log_contains("first:stop") &&
                    log_contains("second:update:1"),
                "explicit application retires old code first"))
    {
        return false;
    }
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

bool run_project_script_cases(const fs::path& workspace)
{
    engine::plugins::script::ScriptSystem script;
    Fixture version;
    Fixture invalid;
    if (!script.initialize({}) ||
        !fixture(workspace, "project-script", {{"scripts/game.js", R"JS(
import {Gameplay,Time} from "engine";
export default class Game extends Gameplay {
    OnInitialize(){this.value=4; return true;}
    Update(){}
    OnShutdown(){}
})JS"}},
                 version) ||
        !fixture(workspace, "project-script-invalid",
                 {{"scripts/game.js", "export default class { syntax error }"}}, invalid) ||
        !apply(script, version))
    {
        return false;
    }
    const auto first_run = script.status().run_id;
    std::string result;
    std::string error;
    const std::string source = "const {Time}=require('engine'); exports.default=(args,game)=>"
                               "({value:game.value+=args,delta:Time.deltaTime});";
    const auto execute = [&](std::uint64_t run, std::string_view code, std::string_view arguments)
    {
        return script.execute_project_script({run, "scripts/mcp-check.js", code, arguments}, result,
                                             error);
    };
    if (!expect(first_run != 0 && execute(first_run, source, "3") &&
                    result == R"({"value":7,"delta":0})" && error.empty(),
                "project script accesses active instance with zero deltaTime") ||
        !expect(!execute(first_run + 1, source, "100") && result.empty() && !error.empty(),
                "stale Run rejected before mutation") ||
        !expect(!execute(first_run, "exports.default=()=>{throw Error('temporary');};", "null") &&
                    script.status().running && script.status().run_id == first_run &&
                    script.status().last_error.empty(),
                "temporary script error preserves Run and gameplay status") ||
        !expect(execute(first_run, source, "0") && result == R"({"value":7,"delta":0})",
                "request following an error can continue") ||
        !expect(!execute(first_run, source, "{") && result.empty() && script.status().running,
                "invalid argument JSON rejected without stopping Run"))
    {
        return false;
    }
    bool worker_rejected = false;
    std::thread worker([&] { worker_rejected = !execute(first_run, source, "5"); });
    worker.join();
    for (const auto path :
         {"../escape.js", "/absolute.js", "scripts/./check.js", "scripts/check.ts"})
    {
        if (!expect(
                !script.execute_project_script({first_run, path, source, "10"}, result, error) &&
                    result.empty() && script.status().run_id == first_run,
                "invalid virtual paths are rejected before execution"))
        {
            return false;
        }
    }
    if (!expect(worker_rejected && script.status().run_id == first_run,
                "project code cannot run on a worker thread") ||
        !expect(!script.prepare_gameplay(invalid.catalog, invalid.mounts) &&
                    script.status().run_id == first_run && execute(first_run, source, "0"),
                "candidate syntax failure keeps active Run callable"))
    {
        return false;
    }
    script.stop_gameplay();
    if (!expect(script.status().run_id == 0 && !execute(first_run, source, "0"),
                "stop invalidates Run before further project calls") ||
        !expect(apply(script, version) && script.status().run_id != 0 &&
                    script.status().run_id != first_run,
                "restarting identical content obtains a distinct Run") ||
        !expect(!execute(first_run, source, "99") && execute(script.status().run_id, source, "0") &&
                    result == R"({"value":4,"delta":0})",
                "old requests cannot mutate a restarted Gameplay instance"))
    {
        return false;
    }
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
    if (!expect(scene != nullptr && test_support::scene_nodes(*scene).size() == 1 &&
                    scene->name() == std::string_view(scene_name, sizeof(scene_name) - 1) &&
                    test_support::scene_nodes(*scene).front().name() ==
                        std::string_view(node_name, sizeof(node_name) - 1),
                "embedded NUL survives JavaScript conversion and native name lookup"))
    {
        return false;
    }
    script.stop_gameplay();
    return expect(world.scene() == nullptr, "boundary fixture releases its own scene");
}

const std::string scene_source = R"js(
import { App, Gameplay, Scene, Time, Debug, Component, Camera, Light, MeshRenderer, Transform } from 'engine';
const topLevelBlocked = Scene.Create('top-level') === null && Time.deltaTime === 0;
const gameLanguage = App.gameLanguage;
export default class Game extends Gameplay {
    cube = null;
    scene = null;
    angle = 0;
    constructor() {
        super();
        this.constructorBlocked = Scene.Create('constructor') === null;
    }
    OnInitialize() {
        if (typeof App.assetsPath !== 'string' || App.assetsPath.length === 0 ||
            'builtinAssetsPath' in App || typeof App.persistentDataPath !== 'string' ||
            typeof App.cachePath !== 'string' || gameLanguage !== 'zh-CN' ||
            App.gameLanguage !== 'zh-CN' || Reflect.set(App, 'gameLanguage', 'fr')) return false;
        if (!topLevelBlocked || !this.constructorBlocked || Time.deltaTime !== 0) return false;
        const discarded = Scene.Create('discarded');
        const oldNode = discarded?.CreateNode('old');
        const oldComponent = oldNode?.AddComponent(MeshRenderer);
        const oldTransform = oldNode?.GetComponent(Transform);
        if (!discarded || !oldNode || !oldComponent || !discarded.Destroy()) return false;
        this.scene = Scene.Create('bound-scene');
        if (!this.scene || discarded.IsValid() || oldNode.IsValid() || oldComponent.IsValid() ||
            oldTransform.SetPosition(1, 2, 3) || oldNode.GetComponent(Transform) !== null || oldNode.AddComponent(Camera) !== null || discarded.Destroy() ||
            oldComponent.SetEnabled(false) || oldComponent.staticInstancingEnabled ||
            oldComponent.staticLodEnabled || oldComponent.SetStaticInstancing(true) ||
            oldComponent.SetStaticLod(true)) return false;
        const cube = this.scene.CreateNode('cube');
        const cameraNode = this.scene.CreateNode('camera');
        const lightNode = this.scene.CreateNode('light');
        if (!cube || !cameraNode || !lightNode || !cube.IsValid()) return false;
        const mesh = cube.AddComponent(MeshRenderer);
        const camera = cameraNode.AddComponent(Camera);
        const light = lightNode.AddComponent(Light);
        if (!mesh || !camera || !light || !(mesh instanceof Component) || !mesh.node.IsValid())
            return false;
        if (camera.frustumCullingEnabled || mesh.staticInstancingEnabled || mesh.staticLodEnabled ||
            !camera.SetFrustumCulling(true) || !camera.frustumCullingEnabled ||
            !mesh.SetStaticInstancing(true) || !mesh.staticInstancingEnabled ||
            !mesh.SetStaticLod(true) || !mesh.staticLodEnabled ||
            camera.SetFrustumCulling(1) || mesh.SetStaticInstancing('true') ||
            mesh.SetStaticLod(null) || !camera.frustumCullingEnabled ||
            !mesh.staticInstancingEnabled || !mesh.staticLodEnabled) return false;
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
            !camera.frustumCullingEnabled ||
            light.SetPoint(-1) || !light.SetPoint(12) || light.SetSpot(10, 40, 20) ||
            !light.SetSpot(12, 15, 35) || !light.SetDirectional() ||
            mesh.materialCount !== 1 || mesh.GetMaterial(1) !== null ||
            mesh.GetMaterial(-1) !== null || mesh.GetMaterial(0.5) !== null ||
            mesh.SetMaterialCount(0) || mesh.SetMaterialCount(17) ||
            !mesh.SetMaterialCount(2) || mesh.materialCount !== 2) return false;
        const slot = mesh.GetMaterial(1);
        if (!slot || !slot.IsValid() || !slot.SetType('builtin://materials/default-unlit') ||
            !slot.SetV4('baseColor', .1, .2, .3, 1) ||
            !slot.SetTexture('baseColorTexture', 'builtin://textures/white-srgb') ||
            slot.GetTexture('baseColorTexture') !== 'builtin://textures/white-srgb' ||
            slot.GetTexture('missing') !== null ||
            slot.SetTexture('missing', 'builtin://textures/white-srgb') ||
            slot.SetTexture('baseColorTexture', 'builtin://meshes/cube') ||
            !mesh.SetMaterialCount(1) || slot.IsValid() || slot.SetFloat('roughness', .4) ||
            slot.GetTexture('baseColorTexture') !== null || mesh.materialCount !== 1 ||
            !mesh.SetMaterialCount(2) || slot.IsValid() || slot.SetV4('baseColor', 1, 0, 0, 1) ||
            !mesh.GetMaterial(1)?.IsValid() || !mesh.SetMaterialCount(1) ||
            !mesh.SetMesh('builtin://meshes/cube') || mesh.SetMesh('missing') ||
            !mesh.material.SetTexture('baseColorTexture', 'builtin://textures/white-srgb')) return false;
        if (!cube.GetComponent(Transform).SetPosition(1, 2, 3) || !cube.GetComponent(Transform).SetScale(2, 2, 2) ||
            !mesh.material.SetShader('builtin://shaders/unlit') || mesh.material.SetShader('shaders/missing') ||
            !mesh.material.SetType('builtin://materials/default-unlit') || mesh.material.SetFloat('metallic', .5) ||
            !mesh.material.SetType('builtin://materials/default-pbr') || !mesh.material.SetV4('baseColor', .2, .3, .4, 1) ||
            !mesh.material.SetFloat('metallic', .7) || !mesh.material.SetFloat('roughness', .2) ||
            !mesh.material.SetType('builtin://materials/default-pbr') || mesh.material.SetType('unknown') ||
            mesh.material.SetFloat('missing', 1) || mesh.material.SetFloat('baseColor', .2) ||
            mesh.material.SetV2('roughness', .1, .2) || mesh.material.SetV3('baseColor', .2, .3, .4) ||
            mesh.material.SetV4('baseColor', 2, .3, .4, 1) ||
            mesh.material.SetFloat('roughness', Infinity) ||
            !mesh.SetEnabled(false) || !mesh.SetEnabled(true) ||
            !cameraNode.GetComponent(Transform).SetPosition(0, 0, 8) || !camera.SetPerspective(45, .1, 200) ||
            !camera.frustumCullingEnabled || !mesh.staticInstancingEnabled || !mesh.staticLodEnabled ||
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

bool run_scene_query_cases(const fs::path& workspace)
{
    constexpr std::string_view source = R"js(
import {Gameplay,Scene,Debug} from 'engine';
function check(value,message) { if (!value) throw new Error(message); }
export default class extends Gameplay {
    OnInitialize() {
        this.scene = Scene.Create('Script query owner');
        this.root = this.scene?.CreateNode('Root');
        const second = this.scene?.CreateNode('Root');
        const branch = this.scene?.CreateNode('Branch');
        const leaf = this.scene?.CreateNode('Leaf');
        const empty = this.scene?.CreateNode('');
        const slash = this.scene?.CreateNode('a/b');
        const backslash = this.scene?.CreateNode('back\\name');
        check(this.root && second && branch && leaf && empty && slash && backslash &&
            branch.SetParent(this.root) && leaf.SetParent(branch) &&
            backslash.SetParent(this.root) && this.root.SetEnabled(false) &&
            this.root.SetVisible(false), 'query setup');
        check(this.scene.FindNode('Root')?.id === this.root.id &&
            this.scene.FindNode('')?.id === empty.id &&
            this.scene.FindNode('a/b')?.id === slash.id &&
            this.scene.FindNode('root') === null &&
            this.root.FindChild('Branch/Leaf')?.id === leaf.id &&
            this.root.FindChild('back\\name')?.id === backslash.id &&
            this.root.FindChild('Leaf') === null, 'exact query semantics');
        for (const path of ['', '/Branch', 'Branch/', 'Branch//Leaf', '.', '..',
            'Branch/.', 'Branch/..', 'Branch/../Leaf', 'branch', null, 3]) {
            check(this.root.FindChild(path) === null, 'invalid child path must fail');
        }
        const duplicate = this.scene.CreateNode('Branch');
        check(duplicate?.SetParent(this.root) &&
            this.scene.FindNode('Branch')?.id === branch.id &&
            this.root.FindChild('Branch') === null &&
            this.root.FindChild('Branch/Leaf') === null && branch.SetParent(second) &&
            this.root.FindChild('Branch')?.id === duplicate.id &&
            second.FindChild('Branch/Leaf')?.id === leaf.id && branch.Destroy() &&
            !leaf.IsValid() && branch.FindChild('Leaf') === null &&
            this.scene.FindNode('Branch')?.id === duplicate.id, 'tree edits and ambiguity');
        return true;
    }
    Update() {
        check(!this.scene.IsValid() && this.scene.FindNode('Root') === null &&
            !this.root.IsValid() && this.root.FindChild('Branch') === null,
            'old Run handles must not acquire nodes in a native replacement scene');
        Debug.Log('queries:stale-rejected');
    }
}
)js";
    engine::WorldSystem world;
    engine::plugins::script::ScriptSystem script(&world);
    Fixture input;
    if (!world.initialize({}) || !script.initialize({}) ||
        !fixture(workspace, "scene-query", {{"scripts/game.js", std::string(source)}}, input) ||
        !expect(apply(script, input), "script queries delegate names, hierarchy and ambiguity"))
    {
        return false;
    }
    world.destroy_scene();
    auto* foreign = world.create_scene("Foreign query owner");
    auto root = foreign ? foreign->create_node("Root") : engine::Node{};
    auto child = foreign ? foreign->create_node("Branch") : engine::Node{};
    if (!expect(root && child && child.set_parent(root) && script.tick(.016) &&
                    script.status().running && log_contains("queries:stale-rejected"),
                "script query handles preserve Run ownership across scene replacement"))
    {
        return false;
    }
    script.stop_gameplay();
    return expect(world.scene() == foreign && root.find_child("Branch") == child,
                  "query fixture cleanup leaves native scene ownership intact");
}

bool run_component_identity_and_state_cases(const fs::path& workspace)
{
    constexpr std::string_view source = R"js(
import { Gameplay, Scene, Component, Transform, Camera, Collider, MeshRenderer } from 'engine';
function check(value, message) { if (!value) throw new Error(message); }
export default class extends Gameplay {
    OnInitialize() {
        const scene = Scene.Create('Component identity');
        this.parent = scene.CreateNode('Parent');
        this.node = scene.CreateNode('Replaced');
        check(this.node.SetParent(this.parent), 'parent link');
        this.transform = this.node.GetComponent(Transform);
        this.camera = this.node.AddComponent(Camera);
        this.collider = this.node.AddComponent(Collider);
        this.mesh = this.node.AddComponent(MeshRenderer);
        this.material = this.mesh.material;
        this.components = [this.transform, this.camera, this.collider, this.mesh];
        check(this.components.every(value => value.IsValid() && value.enabled && value.active &&
              value.visible && value.visibleInHierarchy), 'initial component flags');
        check(this.parent.SetEnabled(false) && this.node.enabled && !this.node.active &&
              this.components.every(value => value.enabled && !value.active && value.IsValid()),
              'ancestor activation preserves local settings and identities');
        check(this.parent.SetVisible(false) && this.node.visible && !this.node.visibleInHierarchy &&
              this.components.every(value => value.visible && !value.visibleInHierarchy),
              'ancestor visibility preserves local component settings');
        check(this.parent.SetEnabled(true) && this.node.active && !this.node.visibleInHierarchy,
              'hidden objects can stay active');
        check(this.parent.SetVisible(true) && this.camera.SetEnabled(false) &&
              !this.camera.enabled && !this.camera.active && this.camera.visibleInHierarchy &&
              this.node.active && this.camera.SetEnabled(true), 'independent component activation');
        check(this.camera.SetVisible(false) && !this.camera.visible &&
              !this.camera.visibleInHierarchy && this.camera.active && this.node.visible &&
              this.camera.SetVisible(true), 'component visibility does not stop logic');
        check(!this.parent.SetEnabled(1) && !this.parent.SetVisible('no') &&
              !this.camera.SetEnabled(null) && !this.camera.SetVisible(1), 'strict boolean flags');
        return true;
    }
    Update() {
        check(this.node.IsValid() && this.components.every(value => !value.IsValid() &&
              !value.SetEnabled(false) && !value.SetVisible(false) && value.enabled === null &&
              value.active === null && value.visible === null && value.visibleInHierarchy === null),
              'all old component handles stay invalid after same-node replacement');
        check(!Component.prototype.SetEnabled.call(this.camera, false) &&
              !Component.prototype.SetVisible.call(this.camera, false) &&
              !this.transform.SetPosition(7, 8, 9) && !this.camera.SetPerspective(40, .1, 50) &&
              !this.collider.SetSphere(5) && !this.mesh.SetMaterialCount(2),
              'base methods and class methods cannot mutate replacement components');
        check(!this.material.IsValid() && !this.material.SetFloat('roughness', .2),
              'material handle retains its original owning component identity');
        for (const type of [Transform, Camera, Collider, MeshRenderer]) {
            const fresh = this.node.GetComponent(type);
            check(fresh && fresh.IsValid() && fresh.enabled && fresh.active && fresh.visible &&
                  fresh.visibleInHierarchy, 'fresh component handle targets replacement');
        }
        check(this.node.GetComponent(Transform).SetPosition(1, 2, 3), 'fresh behavior works');
    }
}
)js";
    engine::WorldSystem world;
    engine::plugins::script::ScriptSystem script(&world);
    Fixture input;
    if (!world.initialize({}) || !script.initialize({}) ||
        !fixture(workspace, "component-identities", {{"scripts/game.js", std::string(source)}},
                 input) ||
        !expect(apply(script, input), "initialize common component identities and hierarchy flags"))
    {
        return false;
    }
    auto node = test_support::scene_nodes(*world.scene()).back();
    if (!expect(node.remove_component(engine::Transform::component_type) &&
                    node.remove_component(engine::Camera::component_type) &&
                    node.remove_component(engine::Collider::component_type) &&
                    node.remove_component(engine::MeshRenderer::component_type) &&
                    node.add_component<engine::Transform>().valid() &&
                    node.add_component<engine::Camera>().valid() &&
                    node.add_component<engine::Collider>().valid() &&
                    node.add_component<engine::MeshRenderer>().valid(),
                "replace four component types without replacing their node") ||
        !expect(script.tick(.01) && script.status().running,
                "old component and material handles reject replacements while new handles work"))
    {
        return false;
    }
    script.stop_gameplay();
    return true;
}

bool run_scene_cases(const fs::path& workspace)
{
    engine::WorldSystem world;
    engine::plugins::script::ScriptSystem script(&world);
    Fixture original;
    if (!expect(world.initialize({}) && script.initialize({}),
                "initialize native scene bindings") ||
        !fixture(workspace, "scene-first", {{"scripts/game.js", scene_source}}, original) ||
        !expect(apply(script, original) && world.scene() != nullptr &&
                    test_support::scene_nodes(*world.scene()).size() == 3,
                "scene, generic node components and typed materials execute"))
    {
        return false;
    }
    const auto cube = test_support::scene_nodes(*world.scene()).front();
    float metallic = 0;
    if (!expect(
            cube.get_component<engine::MeshRenderer>().material().get_float("metallic", metallic) &&
                metallic == .7F && world.scene()->active_camera().valid() && script.tick(.25) &&
                cube.get_component<engine::Transform>().settings().rotation.y() == 2.5,
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
        this.material.SetTexture('baseColorTexture','builtin://textures/white-srgb') ||
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

bool run_persistent_scene_cases(const fs::path& workspace)
{
    constexpr std::string_view source = R"js(
import {Gameplay,Scene,Transform} from 'engine';
function check(value,message) { if (!value) throw new Error(message); }
export default class extends Gameplay {
    phase=0;
    OnInitialize() {
        this.first=Scene.Create('persistent-before');
        this.root=this.first?.CreateNode('persistent-root');
        this.child=this.first?.CreateNode('persistent-child');
        this.temporary=this.first?.CreateNode('temporary');
        this.transform=this.child?.GetComponent(Transform);
        return this.root && !this.root.IsPersistent() && !this.root.SetPersistent(1) &&
            this.root.SetPersistent(true) && this.root.SetPersistent(true) &&
            this.child.SetParent(this.root) && this.child.IsPersistent() &&
            !this.child.SetPersistent(true) && !this.root.SetParent(this.temporary) &&
            this.transform?.SetPosition(1,2,3);
    }
    Update() {
        if (this.phase++===0) {
            check(this.first.Destroy() && !this.first.IsValid() &&
                this.first.CreateNode('stale')===null && !this.temporary.IsValid() &&
                this.root.IsValid() && this.child.IsValid() && this.transform.IsValid(),
                'unload retains the subtree and rejects the old Scene handle');
            this.next=Scene.Create('persistent-after');
            check(this.next && this.next.CreateNode('new') &&
                this.root.FindChild('persistent-child')?.id===this.child.id &&
                this.child.sceneId===this.root.sceneId &&
                Scene.Load('missing.scene')===null && this.next.IsValid() &&
                this.transform.SetPosition(4,5,6), 'live identities survive a new scene and load failure');
        } else {
            check(!this.next.IsValid() && !this.next.Destroy() &&
                Scene.Create('steal')===null && Scene.Load('missing.scene')===null &&
                this.child.SetParent(null) && !this.child.IsPersistent() &&
                this.child.IsValid() && this.root.IsPersistent(),
                'native scene is protected and detached descendants remain Run-owned');
        }
    }
}
)js";
    engine::WorldSystem world;
    engine::plugins::script::ScriptSystem script(&world);
    Fixture initial;
    if (!world.initialize({}) || !script.initialize({}) ||
        !fixture(workspace, "persistent-scene", {{"scripts/game.js", std::string(source)}},
                 initial) ||
        !expect(apply(script, initial), "initialize persistent script nodes"))
    {
        return false;
    }
    auto root = test_support::scene_nodes(*world.scene())[0];
    auto child = test_support::scene_nodes(*world.scene())[1];
    const auto root_id = root.id();
    const auto child_id = child.id();
    if (!expect(script.tick(.016) && script.status().running && world.scenes().size() == 2 &&
                    world.find_node(root_id) == root && world.find_node(child_id) == child,
                "persistent script handles preserve exact native objects"))
    {
        return false;
    }
    Fixture candidate;
    if (!fixture(workspace, "persistent-candidate",
                 {{"scripts/game.js",
                   "import {Gameplay,Scene} from 'engine';"
                   "const blocked=Scene.Create('forbidden')===null;"
                   "export default class extends Gameplay{OnInitialize(){return blocked;}}"}},
                 candidate) ||
        !expect(script.prepare_gameplay(candidate.catalog, candidate.mounts) &&
                    script.prepare_gameplay(candidate.catalog, candidate.mounts) &&
                    world.find_node(root_id) == root && world.scenes().size() == 2,
                "discarding an unactivated candidate cannot destroy another Run's scenes"))
    {
        return false;
    }
    world.unload_scene();
    auto* foreign = world.create_scene("foreign-persistent-owner");
    auto native = foreign ? foreign->create_node("foreign-root") : engine::Node{};
    if (!expect(native && native.set_persistent(true) && script.tick(.016) &&
                    script.status().running && world.find_node(child_id) == child,
                "old Run can detach its own child without acquiring the foreign scene"))
    {
        return false;
    }
    script.stop_gameplay();
    script.shutdown();
    return expect(!world.find_node(root_id) && !world.find_node(child_id) &&
                      world.scene() == foreign && world.scenes().size() == 1 &&
                      test_support::scene_nodes(*foreign).front() == native,
                  "Run shutdown clears every owned container and leaves native ownership intact");
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
    OnInitialize(){this.scene=Scene.Load('scenes/main.scene');
        if(!this.scene || !this.scene.FindNode('GUID Node') || Scene.Load('scenes/missing.scene')!==null)return false;
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
        !write_file(version.root / "gen" / guid_for("scenes/main.scene"), "corrupt") ||
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
import { Gameplay, Scene, Component, Collider, RigidBody, CharacterMotor, AudioSource, AudioListener, Physics } from 'engine';
export default class extends Gameplay {
    OnInitialize() {
        if (Physics.GetImpacts().length !== 0) return false;
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
            !audio.SetDistances(1, 25) || !audio.SetClip('sounds/test.wav') ||
            audio.SetClip('builtin://meshes/cube') || !audio.Play() ||
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
    if (!expect(scene != nullptr && test_support::scene_nodes(*scene).size() == 2,
                "binding scene retains two nodes"))
    {
        return false;
    }
    const auto emitter = test_support::scene_nodes(*scene).front();
    const auto collider = emitter.get_component<engine::Collider>();
    const auto body = emitter.get_component<engine::RigidBody>();
    const auto source_component = emitter.get_component<engine::AudioSource>();
    const auto listener =
        test_support::scene_nodes(*scene).back().get_component<engine::AudioListener>();
    const auto motor =
        test_support::scene_nodes(*scene).back().get_component<engine::CharacterMotor>();
    if (!expect(collider.valid() && body.valid() && source_component.valid() && listener.valid() &&
                    motor.valid() && motor.settings().radius == .4F &&
                    motor.settings().external_damping == 3.5F &&
                    collider.settings().shape == engine::schema::ColliderShape::box &&
                    body.settings().mass == 2 && body.linear_velocity().z() == 3 &&
                    source_component.clip_data() != nullptr && source_component.settings().loop &&
                    source_component.settings().volume == .5F && listener.settings().volume == .75F,
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

#if defined(ENGINE_TEST_EFFECTS_PLUGIN)
bool register_optional_effect_component(engine::WorldSystem& world)
{
    using engine::plugins::effects::EffectComponent;
    auto& registry = world.component_registry();
    auto* scene = world.create_scene("Optional plugin registration");
    auto node = scene ? scene->create_node("Effect probe") : engine::Node{};
    std::vector<engine::Component> before;
    std::vector<engine::Component> after;
    if (!expect(node && !registry.contains(EffectComponent::component_type) &&
                    node.collect_components(before) && !node.add_component<EffectComponent>() &&
                    !node.get_component<EffectComponent>() && node.collect_components(after) &&
                    before == after,
                "an unregistered optional component cannot create partial ECS state") ||
        !expect(registry.add<EffectComponent>() && node.add_component<EffectComponent>(),
                "the host registers effect data without starting a renderer or effect system"))
    {
        return false;
    }
    world.destroy_scene();
    return true;
}
#endif

bool run_impact_binding_cases(const fs::path& workspace)
{
    constexpr std::string_view source = R"js(
import * as api from 'engine';
const { Gameplay, Scene, Transform, Collider, RigidBody, Physics } = api;
const topLevelEmpty = Physics.GetImpacts().length === 0;
export default class extends Gameplay {
    OnInitialize() {
        const scene = Scene.Create('impact-bindings');
        this.scene = scene;
        const floor = scene?.CreateNode('floor');
        this.ball = scene?.CreateNode('ball');
        if (!topLevelEmpty || !floor || !this.ball || Physics.GetImpacts().length !== 0 ||
            !floor.GetComponent(Transform).SetPosition(0, -.5, 0) ||
            !floor.AddComponent(Collider)?.SetBox(5, .5, 5) ||
            !this.ball.GetComponent(Transform).SetPosition(0, 2, 0) ||
            !this.ball.AddComponent(Collider)?.SetSphere(.3) ||
            !this.ball.AddComponent(RigidBody)?.SetMass(2)) return false;
        if (api.EffectComponent) {
            const node = scene.CreateNode('effect-handle');
            const effect = node?.AddComponent(api.EffectComponent);
            if (!effect) throw new Error('The host did not register the optional effect component');
            if (!(effect instanceof api.Component) || effect.Play() ||
                effect.SetSpeed(NaN) || effect.SetSpeed(-1) || effect.SetSpeed(17) ||
                effect.SetLoop('true') || effect.SetPlayOnAwake(1) ||
                !effect.SetSpeed(.5) || !effect.SetLoop(false) ||
                !effect.SetPlayOnAwake(false) || !effect.Stop() || effect.isPlaying ||
                !node.Destroy() || effect.SetSpeed(1) || effect.Play() || effect.isPlaying) return false;
        }
        return true;
    }
    Update() {
        const hits = Physics.GetImpacts();
        if (!hits.length) return;
        const hit = hits[0], repeat = Physics.GetImpacts()[0];
        if (typeof hit.frame !== 'bigint' || typeof hit.a.sceneId !== 'bigint' ||
            typeof hit.a.nodeId !== 'bigint' || typeof hit.b.nodeId !== 'bigint' ||
            hit.a.nodeId === hit.b.nodeId || hit.frame !== repeat?.frame ||
            !Physics.IsObjectValid(hit.a) || !Physics.IsObjectValid(hit.b) ||
            !hit.a.node.IsValid() || !hit.b.node.IsValid() ||
            hit.closingSpeed <= 0 || hit.estimatedImpulse <= 0 ||
            !Number.isFinite(hit.point.y) || Math.abs(hit.normal.y) < .9)
            throw new Error('Invalid physics snapshot');
        if (!this.ball.Destroy() || Physics.GetImpacts().length !== 0)
            throw new Error('Deleted body leaked through binding');
        if (!this.scene.Destroy()) throw new Error('Cannot close old scene');
        const scene = Scene.Create('impact-done');
        if (!scene || Physics.GetImpacts().length !== 0)
            throw new Error('Previous scene impacts leaked');
    }
}
)js";
    engine::WorldSystem world;
    engine::PhysicsSystem physics(&world);
    engine::plugins::script::ScriptSystem script(&world, nullptr, nullptr, &physics);
    Fixture input;
    if (!world.initialize({}) || !physics.initialize({}) || !script.initialize({}) ||
#if defined(ENGINE_TEST_EFFECTS_PLUGIN)
        !register_optional_effect_component(world) ||
#endif
        !fixture(workspace, "impact-bindings", {{"scripts/game.js", std::string(source)}}, input) ||
        !expect(apply(script, input), "initialize impact and optional effect bindings"))
    {
        return false;
    }
    for (int frame = 0; frame != 120 && world.scene()->name() != "impact-done"; ++frame)
    {
        // Same order as RuntimeHost: script reads last frame, physics publishes this frame.
        if (!expect(script.tick(1.0 / 60.0) && physics.tick(1.0 / 60.0),
                    "read physical impact from script without stale bodies or scene leakage"))
        {
            return false;
        }
    }
    const bool complete = world.scene() && world.scene()->name() == "impact-done";
    script.stop_gameplay();
    physics.clear();
    return expect(complete && script.status().last_error.empty() && physics.impacts().empty(),
                  "real drop delivers one validated snapshot and clears on stop");
}

bool run_shooting_query_cases(const fs::path& workspace)
{
    constexpr std::string_view source = R"js(
import { Gameplay, Scene, Transform, Camera, Collider, Physics, Render } from 'engine';
const origin = {x: 0, y: 0, z: 7}, displacement = {x: 0, y: 0, z: -20};
const blockedOutside = Physics.Raycast(origin, displacement) === null &&
    !Physics.IsHitValid({hit: true, sceneId: 1n, nodeId: 1n, instanceId: 1n});
function check(value, message) { if (!value) throw new Error(message); }
function near(a, b) { return Math.abs(a - b) < 0.0001; }
export default class extends Gameplay {
    OnInitialize() {
        this.scene = Scene.Create('shooting-queries');
        this.phase = 0;
        this.target = this.scene?.CreateNode('target');
        this.blocker = this.scene?.CreateNode('blocker');
        const cameraNode = this.scene?.CreateNode('camera');
        this.camera = cameraNode?.AddComponent(Camera);
        if (!blockedOutside || !this.target || !this.blocker || !this.camera ||
            !cameraNode.GetComponent(Transform).SetPosition(0, 0, 8) ||
            !this.camera.SetPerspective(90, 1, 100) ||
            !this.target.AddComponent(Collider)?.SetBox(1, 1, 1) ||
            !this.blocker.GetComponent(Transform).SetPosition(0, 0, 3) ||
            !this.blocker.AddComponent(Collider)?.SetBox(.5, .5, .5)) return false;
        check(typeof this.target.id === 'bigint' && this.target.id !== this.blocker.id &&
            this.target.sceneId === this.blocker.sceneId, 'stable node identities');
        const ray = this.camera.ViewportPointToRay(.5, .5, 2);
        const screen = this.camera.ScreenPointToRay(200, 100, 400, 200);
        check(ray && screen && near(ray.origin.z, 7) && near(ray.direction.z, -1) &&
            near(screen.origin.z, ray.origin.z) && near(screen.direction.x, 0), 'center rays');
        check(this.camera.ViewportPointToRay(.5, .5, 0) === null &&
            this.camera.ScreenPointToRay(0, 0, 0, 100) === null &&
            this.camera.ScreenPointToRay(NaN, 0, 100, 100) === null &&
            Render.GetViewportSize() === null, 'invalid viewport and absent renderer');
        check(this.camera.SetOrthographic(2, 1, 100), 'orthographic setup');
        const left = this.camera.ViewportPointToRay(0, .5, 2);
        const right = this.camera.ViewportPointToRay(1, .5, 2);
        check(left && right && near(left.origin.x, -4) && near(right.origin.x, 4) &&
            near(left.direction.z, right.direction.z), 'parallel orthographic rays');
        return this.camera.SetPerspective(90, 1, 100);
    }
    Update() {
        if (this.phase++ === 0) {
            const wall = Physics.Raycast(origin, displacement);
            check(wall?.hit && Physics.IsHitValid(wall) && wall.node.IsValid() &&
                wall.node.id === this.blocker.id && wall.nodeId === this.blocker.id &&
                wall.sceneId === this.blocker.sceneId && typeof wall.instanceId === 'bigint' &&
                near(wall.point.z, 3.5) && near(wall.normal.z, 1) && near(wall.distance, 3.5) &&
                near(wall.fraction, .175), 'closest wall hit');
            check(Object.isFrozen(wall) && Object.isFrozen(wall.point), 'readonly hit snapshot');
            const sphere = Physics.SphereCast(origin, displacement, .25);
            check(sphere?.hit && sphere.nodeId === this.blocker.id &&
                near(sphere.distance, 3.25) && near(sphere.point.z, 3.5), 'sphere target point');
            this.oldHit = Physics.Raycast(origin, displacement, {ignoreNode: this.blocker});
            check(this.oldHit?.hit && this.oldHit.nodeId === this.target.id &&
                near(this.oldHit.distance, 6), 'ignore foreground body');
            const filtered = Physics.Raycast(origin, displacement, {includeStatic: false});
            check(filtered && !filtered.hit && filtered.node === null && filtered.nodeId === 0n &&
                filtered.distance === 0 && !Physics.IsHitValid(filtered), 'empty query snapshot');
            check(Physics.Raycast(origin, {x:0,y:0,z:0}) === null &&
                Physics.Raycast({x:NaN,y:0,z:0}, displacement) === null &&
                Physics.Raycast(origin, displacement, null) === null &&
                Physics.Raycast(origin, displacement, {includeStatic: 1}) === null &&
                Physics.Raycast(origin, displacement, {ignoreNode: {id: 1n}}) === null &&
                Physics.SphereCast(origin, displacement, 0) === null, 'invalid query boundaries');
            check(this.blocker.Destroy() && !Physics.IsHitValid(wall) && !wall.node.IsValid(),
                'deleted wall cannot remain valid');
            check(Physics.Raycast(origin, displacement)?.nodeId === this.target.id,
                'deleted foreground does not hide surviving target before physics tick');
        } else if (this.phase === 2) {
            check(this.target.IsValid() && !Physics.IsHitValid(this.oldHit),
                'replaced collider invalidates old hit, not node identity');
            const staleBody = Physics.Raycast(origin, displacement);
            check(staleBody && !staleBody.hit, 'replaced collider cannot reuse old Jolt body');
        } else {
            const hit = Physics.Raycast(origin, displacement);
            check(hit?.hit && Physics.IsHitValid(hit) && hit.nodeId === this.target.id &&
                hit.instanceId !== this.oldHit.instanceId, 'new collider owns new hit identity');
            check(this.scene.Destroy(), 'destroy shot scene');
            this.scene = Scene.Create('shooting-done');
            check(this.scene && !hit.node.IsValid() && !Physics.IsHitValid(hit) &&
                this.camera.ViewportPointToRay(.5, .5, 1) === null &&
                Physics.Raycast(origin, displacement, {ignoreNode: this.target}) === null,
                'old scene query and camera identities cannot revive');
        }
    }
}
)js";
    engine::WorldSystem world;
    engine::PhysicsSystem physics(&world);
    engine::plugins::script::ScriptSystem script(&world, nullptr, nullptr, &physics);
    Fixture input;
    if (!world.initialize({}) || !physics.initialize({}) || !script.initialize({}) ||
        !fixture(workspace, "shooting-queries", {{"scripts/game.js", std::string(source)}},
                 input) ||
        !expect(apply(script, input), "initialize camera and shooting query bindings") ||
        !expect(physics.tick(0) && script.tick(.016), "query closest targets from script"))
    {
        return false;
    }
    engine::Node target = {};
    for (const auto& node : test_support::scene_nodes(*world.scene()))
    {
        if (node.name() == "target")
        {
            target = node;
        }
    }
    if (!expect(target && target.remove_component(engine::Collider::component_type) &&
                    target.add_component<engine::Collider>() && script.tick(.016) &&
                    physics.tick(0) && script.tick(.016),
                "query bindings reject replaced collider and expired scene identities"))
    {
        return false;
    }
    const bool complete = world.scene() && world.scene()->name() == "shooting-done" &&
                          script.status().last_error.empty();
    script.stop_gameplay();
    physics.clear();
    return expect(complete, "camera and shooting query script lifecycle completes");
}

bool run_physics_interaction_binding_cases(const fs::path& workspace)
{
    constexpr std::string_view source = R"js(
import { Gameplay, Scene, Transform, CharacterMotor, Collider, Physics } from 'engine';
const center = {x:0,y:.9,z:0};
const blockedOutside = Physics.OverlapSphere(center, 2) === null &&
    Physics.GetContacts().length === 0 && !Physics.IsObjectValid(null);
function check(value, message) { if (!value) throw new Error(message); }
export default class extends Gameplay {
    phase = 0;
    OnInitialize() {
        this.scene = Scene.Create('physics-interactions');
        this.actor = this.scene?.CreateNode('character');
        this.trigger = this.scene?.CreateNode('sensor');
        this.wall = this.scene?.CreateNode('wall');
        this.motor = this.actor?.AddComponent(CharacterMotor);
        this.pose = this.actor?.GetComponent(Transform);
        const sensor = this.trigger?.AddComponent(Collider);
        return blockedOutside && this.motor?.SetCapsule(.3,.6) &&
            this.motor.SetGravityFactor(0) && this.pose && sensor?.SetBox(1,1,1) &&
            sensor.SetTrigger(true) &&
            this.trigger.GetComponent(Transform).SetPosition(0,.9,0) &&
            this.wall?.GetComponent(Transform).SetPosition(0,.9,-4) &&
            this.wall.AddComponent(Collider)?.SetBox(.5,.5,.5);
    }
    Update() {
        const events = Physics.GetContacts();
        const pair = events.filter(e => e.isTrigger &&
            ((e.a.nodeId === this.actor.id && e.b.nodeId === this.trigger.id) ||
             (e.b.nodeId === this.actor.id && e.a.nodeId === this.trigger.id)));
        check(Object.isFrozen(events), 'contact array must be readonly');
        for (const e of pair) {
            check(Object.isFrozen(e) && Object.isFrozen(e.a) && Object.isFrozen(e.normal) &&
                typeof e.frame === 'bigint' && typeof e.step === 'bigint' &&
                e.a.node?.IsValid() && e.b.node?.IsValid(), 'contact identity and readonly values');
        }
        if (this.phase++ === 0) {
            const origin = {x:0,y:.9,z:4}, displacement = {x:0,y:0,z:-10};
            this.hit = Physics.Raycast(origin, displacement);
            check(this.hit?.hit && this.hit.kind === 'character' &&
                this.hit.nodeId === this.actor.id && typeof this.hit.instanceId === 'bigint' &&
                Physics.IsHitValid(this.hit) && Physics.IsObjectValid(this.hit),
                'character participates in the existing closest query');
            check(Physics.SphereCast(origin,displacement,.2)?.kind === 'character' &&
                Physics.Raycast(origin,displacement,{includeCharacters:false})?.nodeId === this.wall.id &&
                Physics.Raycast(origin,displacement,{includeKinematic:false})?.kind === 'character',
                'character filtering is independent of body motion categories');
            for (const result of [Physics.OverlapSphere(center,2,{includeSensors:true}),
                                 Physics.OverlapBox(center,{x:2,y:2,z:2},{includeSensors:true})]) {
                check(result?.length === 2 && Object.isFrozen(result) &&
                    new Set(result.map(o=>o.nodeId)).size === 2 &&
                    result.every(o=>Object.isFrozen(o) && Physics.IsObjectValid(o) && o.node.IsValid()) &&
                    result.some(o=>o.kind === 'character'), 'deduplicated object overlap snapshots');
            }
            check(Physics.OverlapSphere(center,2)?.length === 1 &&
                Physics.OverlapSphere(center,2,{ignoreNode:this.actor})?.length === 0 &&
                Physics.OverlapSphere(center,-1) === null &&
                Physics.OverlapBox(center,{x:1,y:0,z:1}) === null &&
                Physics.OverlapSphere(center,2,{includeCharacters:1}) === null &&
                !Physics.IsObjectValid({...this.hit,kind:'invalid'}), 'overlap and identity boundaries');
            check(pair.some(e=>e.phase==='begin'), 'sensor begin reaches the real script backend');
            this.frame = pair[0].frame;
        } else if (this.phase === 2) {
            check(pair.some(e=>e.phase==='stay' && e.frame > this.frame), 'sensor stay snapshot');
            check(this.pose.SetPosition(5,0,0), 'move character out of sensor');
        } else {
            check(pair.some(e=>e.phase==='end'), 'sensor end snapshot');
            check(Physics.IsObjectValid(this.hit), 'movement preserves historical object identity');
            check(this.motor.SetEnabled(false) && !Physics.IsHitValid(this.hit),
                'disabled character invalidates query identity immediately');
            check(this.scene.Destroy(), 'destroy interaction scene');
            this.scene = Scene.Create('physics-interactions-done');
            check(this.scene && Physics.GetContacts().length === 0 &&
                !Physics.IsObjectValid(this.hit), 'no contact or identity leakage across scenes');
        }
    }
}
)js";
    engine::WorldSystem world;
    engine::PhysicsSystem physics(&world);
    engine::plugins::script::ScriptSystem script(&world, nullptr, nullptr, &physics);
    Fixture input;
    if (!world.initialize({}) || !physics.initialize({}) || !script.initialize({}) ||
        !fixture(workspace, "physics-interactions", {{"scripts/game.js", std::string(source)}},
                 input) ||
        !expect(apply(script, input), "initialize physics interaction bindings"))
    {
        return false;
    }
    for (int frame = 0; frame < 3; ++frame)
    {
        if (!expect(physics.tick(1.0 / 60.0) && script.tick(1.0 / 60.0) &&
                        script.status().running && script.status().last_error.empty(),
                    "physics object, overlap and contact bindings use the real backend"))
        {
            std::fprintf(stderr, "%s\n", script.status().last_error.c_str());
            return false;
        }
    }
    const bool complete = world.scene() && world.scene()->name() == "physics-interactions-done";
    script.stop_gameplay();
    physics.clear();
    return expect(complete && physics.contacts().empty(), "clear contact bindings on stop");
}

bool run_physics_subscription_cases(const fs::path& workspace)
{
    constexpr std::string_view source = R"js(
import {Gameplay,Scene,Transform,Collider,RigidBody,CharacterMotor,Physics} from 'engine';
const blockedOutside = Physics.OnTriggerEnter(null, ()=>{}) === 0 && !Physics.Off(1);
function check(value, message) { if (!value) throw new Error(message); }
function wrong() { throw new Error('collision and trigger subscriptions crossed categories'); }
function validate(e, self, other, trigger) {
    check(Object.isFrozen(e) && Object.isFrozen(e.self) && Object.isFrozen(e.other) &&
        Object.isFrozen(e.point) && Object.isFrozen(e.normal) &&
        typeof e.frame === 'bigint' && typeof e.step === 'bigint' &&
        ['begin','stay','end'].includes(e.phase) && e.isTrigger === trigger &&
        e.self.nodeId === self.id && e.other.nodeId === other.id &&
        Physics.IsObjectValid(e.self) && e.self.node.IsValid() &&
        Number.isFinite(e.point.x) && Number.isFinite(e.normal.y), 'readonly self-oriented contact');
}
export default class extends Gameplay {
    phase=0; trigger={begin:0,stay:0,end:0}; collision={begin:0,stay:0,end:0};
    trace=[]; late=0; survivorEnters=0; exits=[]; freshEnters=0; freshExits=0;
    replacementEnters=0; replacementExits=0;
    OnInitialize() {
        this.scene=Scene.Create('physics-subscriptions');
        this.actor=this.scene?.CreateNode('subscribed-character');
        this.sensor=this.scene?.CreateNode('subscribed-trigger');
        this.floor=this.scene?.CreateNode('subscribed-floor');
        this.solid=this.scene?.CreateNode('subscribed-solid');
        const empty=this.scene?.CreateNode('unconfigured');
        const conflict=this.scene?.CreateNode('conflicting-components');
        this.motor=this.actor?.AddComponent(CharacterMotor);
        this.pose=this.actor?.GetComponent(Transform);
        const sensor=this.sensor?.AddComponent(Collider);
        const floor=this.floor?.AddComponent(Collider);
        const solid=this.solid?.AddComponent(Collider);
        const body=this.solid?.AddComponent(RigidBody);
        const conflictCollider=conflict?.AddComponent(Collider);
        const conflictMotor=conflict?.AddComponent(CharacterMotor);
        check(blockedOutside && this.motor?.SetGravityFactor(0) && this.pose &&
            sensor?.SetBox(1,1,1) && sensor.SetTrigger(true) &&
            this.sensor.GetComponent(Transform).SetPosition(0,.9,0) &&
            floor?.SetBox(2,.5,2) && this.floor.GetComponent(Transform).SetPosition(10,-.5,0) &&
            solid?.SetBox(.5,.5,.5) && body?.SetMotion('kinematic') &&
            this.solid.GetComponent(Transform).SetPosition(10,.4,0), 'subscription scene setup');
        check(Physics.OnTriggerEnter(empty, ()=>{}) === 0 &&
            Physics.OnTriggerEnter({id:this.actor.id}, ()=>{}) === 0 &&
            Physics.OnTriggerEnter(this.actor, null) === 0 &&
            Physics.OnTriggerEnter(conflict, ()=>{}) === 0 &&
            conflictCollider.SetEnabled(false) && conflictMotor.SetEnabled(false) &&
            Physics.OnCollisionEnter(conflict, ()=>{}) === 0 &&
            !Physics.Off(0) && !Physics.Off(-1) && !Physics.Off(.5) && !Physics.Off(NaN),
            'invalid, ambiguous and disabled subscription targets fail');
        this.enterId=Physics.OnTriggerEnter(this.actor, e=>{
            validate(e,this.actor,this.sensor,true);
            this.trigger.begin++; this.trace.push(e);
            this.lateId=Physics.OnTriggerStay(this.actor, ()=>{this.late++;});
            check(this.lateId > 0 && Physics.Off(this.cancelId), 'subscribe and cancel inside callback');
        });
        this.stayId=Physics.OnTriggerStay(this.actor, e=>{
            validate(e,this.actor,this.sensor,true); this.trigger.stay++; this.trace.push(e);
        });
        this.exitId=Physics.OnTriggerExit(this.actor, e=>{
            validate(e,this.actor,this.sensor,true); this.trigger.end++;
        });
        this.cancelId=Physics.OnTriggerStay(this.actor, ()=>{
            throw new Error('Off must take effect within the current dispatch batch');
        });
        this.sensorEnterId=Physics.OnTriggerEnter(this.sensor, e=>{
            validate(e,this.sensor,this.actor,true); this.survivorEnters++;
        });
        this.sensorExitId=Physics.OnTriggerExit(this.sensor, e=>{
            validate(e,this.sensor,this.actor,true); this.exits.push(Physics.IsObjectValid(e.other));
        });
        this.collisionEnter=Physics.OnCollisionEnter(this.solid, e=>{
            validate(e,this.solid,this.floor,false);
            check(e.normal.y < -.99, 'collision normal points from self down to floor');
            this.collision.begin++;
        });
        this.collisionStay=Physics.OnCollisionStay(this.solid, e=>{
            validate(e,this.solid,this.floor,false); this.collision.stay++;
        });
        this.collisionExit=Physics.OnCollisionExit(this.solid, e=>{
            validate(e,this.solid,this.floor,false); this.collision.end++;
        });
        const inverse=Physics.OnCollisionEnter(this.floor, e=>{
            validate(e,this.floor,this.solid,false);
            check(e.normal.y > .99, 'opposite subscriber receives the inverted normal');
        });
        const ids=[this.enterId,this.stayId,this.exitId,this.cancelId,this.sensorEnterId,
            this.sensorExitId,this.collisionEnter,this.collisionStay,this.collisionExit,inverse,
            Physics.OnCollisionEnter(this.actor,wrong),Physics.OnCollisionStay(this.actor,wrong),
            Physics.OnCollisionExit(this.actor,wrong),Physics.OnTriggerEnter(this.solid,wrong),
            Physics.OnTriggerStay(this.solid,wrong),Physics.OnTriggerExit(this.solid,wrong)];
        check(ids.every(id=>Number.isSafeInteger(id) && id>0) && new Set(ids).size===ids.length,
            'OnInitialize captures unique subscriptions before physics synchronization');
        return true;
    }
    Update() {
        switch(this.phase++) {
        case 0:
            check(this.trigger.begin===1 && this.trigger.stay===2 && this.trigger.end===0 &&
                this.collision.begin===1 && this.collision.stay===2 && this.collision.end===0 &&
                this.survivorEnters===1 && this.late===0 && this.trace.length===3 &&
                this.trace.every(e=>e.frame===this.trace[0].frame) &&
                this.trace[1].step===this.trace[0].step+1n &&
                this.trace[2].step===this.trace[1].step+1n,
                'dispatch precedes Update and retains every substep without new subscribers');
            break;
        case 1:
            check(this.trace.length===3 && this.collision.stay===2 && this.late===0,
                'script-only tick cannot replay the previous physics frame');
            break;
        case 2:
            check(this.trigger.stay===3 && this.collision.stay===3 && this.late===1 &&
                Physics.Off(this.collisionStay) && !Physics.Off(this.collisionStay) &&
                this.motor.SetEnabled(false), 'next batch activates late subscribers and explicit Off');
            break;
        case 3:
            check(this.exits.length===1 && this.exits[0]===false && this.trigger.end===0 &&
                !Physics.Off(this.enterId) && !Physics.Off(this.stayId) &&
                !Physics.Off(this.exitId) && !Physics.Off(this.lateId),
                'disabled owner cancels all subscriptions while its live counterpart receives Exit');
            check(this.motor.SetEnabled(true), 'reenable motor');
            this.freshEnterId=Physics.OnTriggerEnter(this.actor, ()=>{this.freshEnters++;});
            this.freshExitId=Physics.OnTriggerExit(this.actor, ()=>{this.freshExits++;});
            check(this.freshEnterId>0 && this.freshExitId>0, 'explicit subscription after enable');
            break;
        case 4:
            check(this.freshEnters===1 && this.trigger.begin===1 && this.survivorEnters===2 &&
                this.collision.stay===3 && this.late===1,
                'reenabling cannot revive cancelled subscriptions');
            check(this.pose.SetPosition(5,0,0) &&
                this.solid.GetComponent(Transform).SetPosition(15,.4,0), 'separate both pairs');
            break;
        case 5:
            check(this.freshExits===1 && this.exits.length===2 && this.exits[1]===true &&
                this.collision.end===1, 'live separation delivers trigger and collision exits');
            check(this.pose.SetPosition(0,0,0), 'return character before replacing its motor');
            break;
        case 6:
            check(this.freshEnters===2 && this.survivorEnters===3, 'second contact interval begins');
            break;
        case 7:
            check(this.exits.length===3 && this.exits[2]===false && this.freshExits===1 &&
                !Physics.Off(this.freshEnterId) && !Physics.Off(this.freshExitId),
                'same-node replacement cancels captured component generation');
            check(Physics.OnTriggerEnter(this.actor, ()=>{this.replacementEnters++;})>0 &&
                Physics.OnTriggerExit(this.actor, ()=>{this.replacementExits++;})>0,
                'new motor receives independent subscriptions');
            break;
        case 8:
            check(this.replacementEnters===1 && this.freshEnters===2 && this.survivorEnters===4 &&
                this.actor.Destroy(), 'replacement callbacks execute once before node deletion');
            break;
        case 9:
            check(this.exits.length===4 && this.exits[3]===false && this.replacementExits===0 &&
                this.scene.Destroy() && Scene.Create('physics-subscriptions-done'),
                'deleted owner stops callbacks while surviving trigger receives historical Exit');
            break;
        }
    }
}
)js";
    engine::WorldSystem world;
    engine::PhysicsSystem physics(&world);
    engine::plugins::script::ScriptSystem script(&world, nullptr, nullptr, &physics);
    Fixture input;
    if (!world.initialize({}) || !physics.initialize({}) || !script.initialize({}) ||
        !fixture(workspace, "physics-subscriptions", {{"scripts/game.js", std::string(source)}},
                 input) ||
        !expect(apply(script, input), "subscribe to physics callbacks during initialization"))
    {
        return false;
    }
    constexpr double step = 1.0 / 60.0;
    const std::array intervals{3 * step, -1.0, step, 0.0, step, step, step, 0.0, step, 0.0};
    for (std::size_t phase = 0; phase < intervals.size(); ++phase)
    {
        if (phase == 7)
        {
            engine::Node actor = {};
            for (const auto& node : test_support::scene_nodes(*world.scene()))
            {
                if (node.name() == "subscribed-character")
                {
                    actor = node;
                }
            }
            if (!actor || !actor.remove_component(engine::CharacterMotor::component_type))
            {
                return false;
            }
            auto motor = actor.add_component<engine::CharacterMotor>();
            if (!motor)
            {
                return false;
            }
            auto settings = motor.settings();
            settings.gravity_factor = 0;
            if (!motor.set_settings(settings))
            {
                return false;
            }
        }
        if (!expect((intervals[phase] < 0 || physics.tick(intervals[phase])) && script.tick(step) &&
                        script.status().running && script.status().last_error.empty(),
                    "physics subscriptions retain ordering, cancellation and component identity"))
        {
            std::fprintf(stderr, "subscription phase %zu: %s\n", phase,
                         script.status().last_error.c_str());
            return false;
        }
    }
    const bool complete = world.scene() && world.scene()->name() == "physics-subscriptions-done";
    script.stop_gameplay();
    physics.clear();
    return expect(complete, "physics subscription lifecycle reaches completion");
}

bool run_physics_subscription_scene_cases(const fs::path& workspace)
{
    constexpr std::string_view source = R"js(
import {Gameplay,Scene,Transform,Collider,CharacterMotor,Physics} from 'engine';
function check(value,message) { if (!value) throw new Error(message); }
export default class extends Gameplay {
    switches=0; newEnters=0; phase=0;
    populate(name) {
        const scene=Scene.Create(name), actor=scene?.CreateNode('actor'), sensor=scene?.CreateNode('sensor');
        check(actor?.AddComponent(CharacterMotor)?.SetGravityFactor(0) &&
            sensor?.AddComponent(Collider)?.SetTrigger(true) &&
            sensor.GetComponent(Transform).SetPosition(0,.9,0), 'scene-switch subscription setup');
        return {scene,actor,sensor};
    }
    OnInitialize() {
        this.old=this.populate('physics-subscription-before-switch');
        check(Physics.OnTriggerEnter(this.old.actor, ()=>{
            this.switches++;
            check(this.old.scene.Destroy(), 'destroy scene from physics callback');
            this.next=this.populate('physics-subscription-after-switch');
            check(Physics.OnTriggerEnter(this.next.actor, ()=>{this.newEnters++;})>0,
                'new-scene subscription from callback');
        })>0, 'initial switching callback');
        const stale=()=>{throw new Error('old scene batch continued after callback switched scene');};
        this.oldId=Physics.OnTriggerEnter(this.old.actor,stale);
        return this.oldId>0 && Physics.OnTriggerStay(this.old.actor,stale)>0 &&
            Physics.OnTriggerExit(this.old.actor,stale)>0;
    }
    Update() {
        if (this.switches===0) {
            check(this.newEnters===0, 'restart cannot deliver the previous run physics batch');
            return;
        }
        check(this.switches===1 && !Physics.Off(this.oldId), 'scene switch cancels old subscription IDs');
        if (this.phase++<2) {
            check(this.newEnters===0, 'new scene cannot consume any event from the old batch');
        } else {
            check(this.newEnters===1 && this.next.scene.Destroy() &&
                Scene.Create('physics-subscription-switch-done'), 'next physics batch reaches new scene');
        }
    }
}
)js";
    engine::WorldSystem world;
    engine::PhysicsSystem physics(&world);
    engine::plugins::script::ScriptSystem script(&world, nullptr, nullptr, &physics);
    if (!world.initialize({}) || !physics.initialize({}) || !script.initialize({}))
    {
        return false;
    }
    constexpr double step = 1.0 / 60.0;
    for (const char* revision :
         {"physics-subscription-switch-first", "physics-subscription-switch-next"})
    {
        Fixture input;
        if (!fixture(workspace, revision, {{"scripts/game.js", std::string(source)}}, input) ||
            !expect(apply(script, input) && script.tick(step) && physics.tick(3 * step) &&
                        script.tick(step) && script.tick(step) && physics.tick(step) &&
                        script.tick(step) && script.status().running &&
                        script.status().last_error.empty() && world.scene() &&
                        world.scene()->name() == "physics-subscription-switch-done",
                    "callback scene switch stops the old batch and new runs isolate subscriptions"))
        {
            std::fprintf(stderr, "%s\n", script.status().last_error.c_str());
            return false;
        }
    }
    script.stop_gameplay();
    physics.clear();
    return expect(script.tick(step) && !script.status().running && world.scene() == nullptr,
                  "stopped gameplay cannot retain or dispatch physics subscriptions");
}

bool run_persistent_physics_subscription_cases(const fs::path& workspace)
{
    constexpr std::string_view source = R"js(
import {Gameplay,Scene,Transform,Collider,CharacterMotor,Physics} from 'engine';
function check(value,message) { if (!value) throw new Error(message); }
export default class extends Gameplay {
    phase=0; enters=0; exits=0;
    addSensor(scene) {
        const sensor=scene.CreateNode('sensor');
        check(sensor?.GetComponent(Transform).SetPosition(0,.9,0) &&
            sensor.AddComponent(Collider)?.SetTrigger(true), 'configure trigger');
        return sensor;
    }
    OnInitialize() {
        this.scene=Scene.Create('persistent-contact-before');
        this.actor=this.scene?.CreateNode('persistent-actor');
        check(this.actor?.SetPersistent(true) &&
            this.actor.AddComponent(CharacterMotor)?.SetGravityFactor(0), 'persistent actor');
        this.oldSensor=this.addSensor(this.scene);
        this.enter=Physics.OnTriggerEnter(this.actor,event=>{
            check(event.self.node.id===this.actor.id && event.self.node.IsPersistent() &&
                Physics.IsObjectValid(event.self) && Physics.IsObjectValid(event.other),
                'contact preserves actual receiver ownership');
            ++this.enters;
        });
        this.exit=Physics.OnTriggerExit(this.actor,event=>{
            check(Physics.IsObjectValid(event.self) && !Physics.IsObjectValid(event.other) &&
                !event.other.node.IsValid(), 'Exit carries removed object history to live receiver');
            ++this.exits;
        });
        return this.enter>0 && this.exit>0;
    }
    Update() {
        if (this.phase++===0) {
            check(this.enters===1 && this.exits===0 && this.scene.Destroy(), 'first trigger enter');
            this.scene=Scene.Create('persistent-contact-after');
            this.newSensor=this.addSensor(this.scene);
        } else if (this.phase===2) {
            check(this.enters===2 && this.exits===1 && this.actor.IsValid() &&
                !this.oldSensor.IsValid(), 'same subscriptions deliver cross-scene enter and old exit');
            const hits=Physics.OverlapSphere({x:0,y:.9,z:0},2,{includeSensors:true});
            check(hits?.length===2 && new Set(hits.map(hit=>hit.sceneId)).size===2 &&
                hits.every(hit=>Physics.IsObjectValid(hit) && hit.node.IsValid()),
                'world queries return accurate scene identities');
            check(this.scene.Destroy() && this.actor.IsValid(), 'leave only the persistent actor');
        } else {
            check(this.exits===2 && this.enters===2 &&
                Physics.GetContacts().some(event=>event.phase==='end') &&
                Physics.OverlapSphere({x:0,y:.9,z:0},2)?.length===1 &&
                Physics.Off(this.enter) && Physics.Off(this.exit) &&
                Scene.Create('persistent-contact-done'),
                'persistent subscriptions and queries work without an active gameplay scene');
        }
    }
}
)js";
    engine::WorldSystem world;
    engine::PhysicsSystem physics(&world);
    engine::plugins::script::ScriptSystem script(&world, nullptr, nullptr, &physics);
    Fixture input;
    if (!world.initialize({}) || !physics.initialize({}) || !script.initialize({}) ||
        !fixture(workspace, "persistent-physics", {{"scripts/game.js", std::string(source)}},
                 input) ||
        !expect(apply(script, input), "initialize persistent contact receiver"))
    {
        return false;
    }
    for (int frame = 0; frame < 3; ++frame)
    {
        if (!expect(physics.tick(1.0 / 60.0) && script.tick(1.0 / 60.0) &&
                        script.status().running && script.status().last_error.empty(),
                    "persistent script callbacks survive scene unload and world-only intervals"))
        {
            std::fprintf(stderr, "%s\n", script.status().last_error.c_str());
            return false;
        }
    }
    const bool complete = world.scene() && world.scene()->name() == "persistent-contact-done";
    script.stop_gameplay();
    return expect(complete && world.scenes().empty(), "stop releases persistent physics ownership");
}

bool run_physics_subscription_promise_case(const fs::path& workspace)
{
    constexpr std::string_view source = R"js(
import {Gameplay,Scene,Transform,Collider,CharacterMotor,Physics,Debug} from 'engine';
export default class extends Gameplay {
    OnInitialize() {
        const scene=Scene.Create('physics-promise-callback');
        const actor=scene?.CreateNode('actor'), sensor=scene?.CreateNode('sensor');
        return actor?.AddComponent(CharacterMotor)?.SetGravityFactor(0) &&
            sensor?.AddComponent(Collider)?.SetTrigger(true) &&
            sensor.GetComponent(Transform).SetPosition(0,.9,0) &&
            Physics.OnTriggerEnter(actor, ()=>Promise.resolve())>0;
    }
    Update() { throw new Error('Update must not run after an asynchronous physics callback'); }
    OnShutdown() { Debug.Log('physics-subscription-promise:shutdown'); }
}
)js";
    engine::WorldSystem world;
    engine::PhysicsSystem physics(&world);
    engine::plugins::script::ScriptSystem script(&world, nullptr, nullptr, &physics);
    Fixture input;
    if (!world.initialize({}) || !physics.initialize({}) || !script.initialize({}) ||
        !fixture(workspace, "physics-subscription-promise",
                 {{"scripts/game.js", std::string(source)}}, input) ||
        !expect(apply(script, input) && physics.tick(1.0 / 60.0) && script.tick(1.0 / 60.0) &&
                    !script.status().running &&
                    script.status().last_error.find("synchronous") != std::string::npos &&
                    log_contains("physics-subscription-promise:shutdown"),
                "physics subscriptions reject Promise results before Update and run shutdown"))
    {
        std::fprintf(stderr, "%s\n", script.status().last_error.c_str());
        return false;
    }
    physics.clear();
    return true;
}

bool run_json_binding_cases(const fs::path& workspace)
{
    // 不注入 WorldSystem：配置读取不能要求先创建场景。
    engine::plugins::script::ScriptSystem script;
    constexpr std::string_view source = R"js(
import { Gameplay, Json, Debug } from 'engine';
const topLevelBlocked = Json.Load('config/maps.json') === null;
function checkValues(revision) {
    const value = Json.Load('config/maps.json');
    const array = Json.Load('config/array.json');
    return value !== null && Object.getPrototypeOf(value) === Object.prototype &&
        value.title === '场景😀' && value.revision === revision && value.enabled === true &&
        value.none === null && Array.isArray(value.maps) && value.maps[0].id === 'sample' &&
        value.settings.exposure === 1.2 &&
        Array.isArray(array) && array.length === 5 && array[0] === 7 &&
        array[1] === false && array[2] === 'label' && array[3] === null &&
        array[4].nested === 8 && Json.Load('config/number.json') === 42 &&
        Json.Load('config/false.json') === false && Json.Load('config/null.json') === null &&
        Json.Load('config/string.json') === 'text 中文' &&
        Json.Load('config/nul-string.json') === 'a\u0000b' &&
        Json.Load('config/maximum.json') === true;
}
function checkFailures() {
    const calls = [
        () => Json.Load(),
        () => Json.Load(null),
        () => Json.Load(true),
        () => Json.Load(42),
        () => Json.Load({}),
        () => Json.Load('config/maps.json', 'extra'),
        () => Json.Load(''),
        () => Json.Load('../loose.json'),
        () => Json.Load(physicalJsonPath),
        () => Json.Load('config/maps.json\u0000trailing'),
        () => Json.Load('config/missing.json'),
        () => Json.Load('config/unavailable.json'),
        () => Json.Load('scripts/looks-like-json.js'),
        () => Json.Load('config/invalid.json'),
        () => Json.Load('config/invalid-utf8.json'),
        () => Json.Load('config/empty.json'),
        () => Json.Load('config/oversized.json'),
    ];
    return calls.every(call => call() === null);
}
function checkIndependentValues() {
    const first = Json.Load('config/maps.json');
    const second = Json.Load('config/maps.json');
    if (first === second || first.maps === second.maps ||
        first.maps[0] === second.maps[0] || first.settings === second.settings) return false;
    first.maps[0].id = 'changed';
    first.maps.push({ id: 'extra' });
    first.settings.exposure = 9;
    const fresh = Json.Load('config/maps.json');
    return second.maps.length === 1 && second.maps[0].id === 'sample' &&
        second.settings.exposure === 1.2 && fresh !== second &&
        fresh.maps.length === 1 && fresh.maps[0].id === 'sample' &&
        fresh.settings.exposure === 1.2;
}
export default class extends Gameplay {
    frame = 0;
    inactiveBlocked = false;
    constructor() {
        super();
        this.constructorBlocked = Json.Load('config/maps.json') === null;
    }
    checkOutsideCallback() {
        this.inactiveBlocked = false;
        Promise.resolve().then(() => {
            this.inactiveBlocked = Json.Load('config/maps.json') === null;
            if (!this.inactiveBlocked) throw new Error('JSON read outside a gameplay callback');
        });
    }
    OnInitialize() {
        this.lastRevision = expectedRevision;
        this.checkOutsideCallback();
        return topLevelBlocked && this.constructorBlocked && Object.isFrozen(Json) &&
            checkValues(expectedRevision) && checkFailures() && checkIndependentValues();
    }
    Update() {
        const revision = expectedRevision === 1 && ++this.frame > 1 ? 2 : expectedRevision;
        if (!this.inactiveBlocked || !checkValues(revision))
            throw new Error('JSON callback state or current catalog is incorrect');
        this.lastRevision = revision;
        this.checkOutsideCallback();
        Debug.Log('json:update:' + expectedRevision + ':' + revision);
    }
    OnShutdown() {
        if (Json.Load('config/maps.json')?.revision !== this.lastRevision)
            throw new Error('JSON read unavailable during shutdown');
        Debug.Log('json:shutdown:' + expectedRevision + ':' + this.lastRevision);
    }
}
)js";
    constexpr std::size_t maximum_bytes = 4U * 1024U * 1024U;
    std::string maximum(maximum_bytes, ' ');
    maximum.replace(0, 4, "true");
    std::string oversized(maximum_bytes + 1U, ' ');
    oversized.replace(0, 4, "true");
    std::map<std::string, std::string> json_resources{
        {"config/maps.json",
         R"json({"title":"场景😀","revision":1,"enabled":true,"none":null,"maps":[{"id":"sample"}],"settings":{"exposure":1.2}})json"},
        {"config/array.json", R"json([7,false,"label",null,{"nested":8}])json"},
        {"config/number.json", "42"},
        {"config/false.json", "false"},
        {"config/null.json", "null"},
        {"config/string.json", R"json("text \u4e2d\u6587")json"},
        {"config/nul-string.json", R"json("a\u0000b")json"},
        {"config/invalid.json", R"json({"broken":)json"},
        {"config/invalid-utf8.json", std::string("\x22\xc0\xaf\x22", 4)},
        {"config/empty.json", ""},
        {"config/maximum.json", std::move(maximum)},
        {"config/oversized.json", std::move(oversized)},
        {"config/unavailable.json", "true"}};
    const auto physical_json = workspace / "loose.json";
    const auto make_source = [&](int revision)
    {
        return "const expectedRevision = " + std::to_string(revision) +
               "; const physicalJsonPath = " +
               Json(engine::PathUtils::to_utf8(physical_json)).dump() + ";\n" + std::string(source);
    };
    Fixture first;
    Fixture next;
    if (!script.initialize({}) || !write_file(physical_json, R"json({"host_file":true})json") ||
        !fixture(workspace, "json-first",
                 {{"scripts/game.js", make_source(1)},
                  {"scripts/looks-like-json.js", R"js("not a JSON resource")js"}},
                 first, {}, {}, json_resources))
    {
        return false;
    }
    json_resources["config/maps.json"] =
        R"json({"title":"场景😀","revision":2,"enabled":true,"none":null,"maps":[{"id":"sample"}],"settings":{"exposure":1.2}})json";
    if (!fixture(workspace, "json-next",
                 {{"scripts/game.js", make_source(2)},
                  {"scripts/looks-like-json.js", R"js("not a JSON resource")js"}},
                 next, {}, {}, json_resources))
    {
        return false;
    }
    std::error_code error;
    if (!fs::remove(first.root / "gen" / guid_for("config/unavailable.json"), error) || error ||
        !fs::remove(next.root / "gen" / guid_for("config/unavailable.json"), error) || error)
    {
        return false;
    }
    auto* resources = engine::ResourceSystem::instance();
    // 顶层和构造测试前即发布真实可读目录，避免因路径缺失而误判为权限拒绝。
    if (!expect(resources != nullptr &&
                    resources->update_catalog(first.catalog.data(), first.mounts) &&
                    apply(script, first) && script.status().running,
                "JSON resources load in initialization without a scene; top level and constructor "
                "stay blocked; primitive, size, UTF-8, kind and parse contracts hold") ||
        !expect(script.tick(.01) && script.status().running && log_contains("json:update:1:1"),
                "JSON reads work in Update while queued jobs outside callbacks return null") ||
        !expect(resources->update_catalog(next.catalog.data(), next.mounts) && script.tick(.01) &&
                    script.status().running && script.status().active_revision == "json-first" &&
                    log_contains("json:update:1:2"),
                "JSON follows the current catalog generation without replacing the script Run") ||
        !expect(apply(script, next) && script.tick(.01) && script.status().running &&
                    log_contains("json:shutdown:1:2") && log_contains("json:update:2:2"),
                "replacement Run parses independent JSON values and shutdown can still read"))
    {
        std::fprintf(stderr, "%s\n", script.status().last_error.c_str());
        return false;
    }
    script.stop_gameplay();
    return expect(script.status().last_error.empty() && log_contains("json:shutdown:2:2"),
                  "JSON facade shuts down cleanly after the final permitted callback");
}

bool run_runtime_control_cases(const fs::path& workspace)
{
    engine::WindowSystem window;
    engine::WorldSystem world;
    if (!window.initialize({}) || !world.initialize({}))
    {
        return false;
    }
    SDL_HideWindow(window.native_window());
    engine::plugins::script::ScriptSystem script(&world, nullptr, nullptr, nullptr, nullptr,
                                                 nullptr, &window);
    constexpr std::string_view source = R"js(
import {Gameplay,Window,Scene,App,Render,Time} from 'engine';
const blocked = Window.GetSize() === null && Window.GetMode() === null &&
    !Window.SetMouseMode('hidden') && !App.RequestQuit() && !Scene.BeginLoad('scenes/main.scene');
function check(value, message) {
    if (!value) throw new Error('Runtime controls: ' + message);
}
export default class extends Gameplay {
    constructor() { super(); this.blocked = !App.RequestQuit(); }
    OnInitialize() {
        this.scene = Scene.Create('paused-window-test');
        const size = Window.GetSize();
        check(blocked && this.blocked, 'module and constructor calls must be blocked');
        check(this.scene && size && size.logicalWidth > 0 && size.pixelWidth > 0,
              'active callback must query actual window sizes');
        check(Window.GetMode() === 'windowed', 'query windowed mode');
        check(Array.isArray(Window.GetDisplayModes()), 'query display modes');
        check(!Window.SetSize(0, 10) && !Window.SetSize(1.5, 20), 'reject invalid logical size');
        check(!Window.SetMode('invalid') &&
              !Window.SetMode('fullscreen', {width:1,height:1,pixelDensity:0,refreshRate:60}),
              'reject invalid display modes');
        check(!Window.SetMouseMode('invalid') && !Window.SetMouseMode(true),
              'reject invalid mouse modes');
        check(Window.SetMouseMode('hidden') && Window.GetMouseMode() === 'hidden',
              'set and query hidden mouse mode');
        check(!App.RequestQuit(1), 'invalid quit arguments must not request host exit');
        check(!Render.SetVSync(false) && Render.GetVSync() === null,
              'missing renderer must reject VSync access');
        check(!this.scene.SetPaused('true') && this.scene.SetPaused(true) &&
              this.scene.GetPaused() === true, 'typed scene pause must reach the world');
        check(Scene.StepLoad(0) === null && Scene.CommitLoad() === null &&
              !Scene.BeginLoad('missing.scene'), 'invalid scene-load operations must fail');
        return true;
    }
    Update() {
        if (!(Time.deltaTime > 0) || !this.scene.GetPaused() || !App.RequestQuit())
            throw new Error('Paused world must retain active UI/gameplay callbacks');
    }
}
)js";
    Fixture input;
    if (!script.initialize({}) ||
        !fixture(workspace, "runtime-controls", {{"scripts/game.js", std::string(source)}},
                 input) ||
        !expect(apply(script, input) && world.paused() && !window.exit_requested(),
                "window controls require active callbacks and pause does not request exit") ||
        !expect(script.tick(.5) && script.status().running && window.exit_requested(),
                "paused gameplay still handles an orderly exit request"))
    {
        std::fprintf(stderr, "%s\n", script.status().last_error.c_str());
        return false;
    }
    script.stop_gameplay();
    engine::MouseMode mode;
    return expect(world.scene() == nullptr && !world.paused() && window.mouse_mode(mode) &&
                      mode == engine::MouseMode::normal,
                  "Run cleanup restores mouse and clears its paused scene");
}

bool run_step_loading_cases(const fs::path& workspace)
{
#if defined(ENGINE_TEST_RESOURCE_GRAPH)
    auto& resources = *engine::ResourceSystem::instance();
    engine::WorldSystem world;
    engine::plugins::resource_graph::ResourceGraphSystem graph(resources);
    if (!world.initialize({}) || !graph.initialize({}))
    {
        return false;
    }
    auto* prototype = world.create_scene("step-loaded-scene");
    engine::schema::SceneDataT data;
    if (!prototype || !prototype->create_node("Prepared node") || !prototype->build_data(data))
    {
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    engine::schema::FinishSceneDataBuffer(builder, engine::schema::SceneData::Pack(builder, &data));
    const std::string bytes(reinterpret_cast<const char*>(builder.GetBufferPointer()),
                            builder.GetSize());
    world.destroy_scene();
    resources.set_loader(&graph);
    const bool passed = [&]()
    {
        engine::plugins::script::ScriptSystem script(&world);
        constexpr std::string_view source = R"js(
import {Gameplay,Scene,Debug} from 'engine';
const blocked = !Scene.BeginLoad('scenes/main.scene') && Scene.StepLoad(0) === null;
export default class extends Gameplay {
    phase=0;
    OnInitialize() {
        this.old = Scene.Create('kept-loading-menu');
        this.loading = this.old?.CreateNode('persistent-loading-root');
        if (!blocked || !this.old || !this.loading?.SetPersistent(true) || !this.old.SetPaused(true) ||
            !Scene.BeginLoad('scenes/main.scene') || Scene.BeginLoad('scenes/main.scene') ||
            Scene.StepLoad(-1) !== null || Scene.StepLoad(0.5) !== null ||
            Scene.StepLoad('1') !== null || Scene.CommitLoad() !== null) return false;
        const progress = Scene.StepLoad(0);
        return progress?.completed === 0 && progress.total === 1 && progress.status === 'loading';
    }
    Update() {
        if (this.phase === 0) {
            const progress = Scene.StepLoad(1);
            if (!this.old.IsValid() || this.old.FindNode('Prepared node') !== null)
                throw new Error('Loading exposed the candidate through the active scene');
            if (progress?.status === 'loading') return;
            if (progress?.status !== 'ready' || progress.completed !== progress.total)
                throw new Error('Budgeted progress or menu lifetime is invalid');
        } else if (this.phase === 1) {
            if (Scene.CommitLoad() !== null || !this.old.IsValid() || !this.old.GetPaused())
                throw new Error('A changed catalog must preserve the paused old scene');
            const failed = Scene.StepLoad(0);
            if (failed?.status !== 'failed' || !failed.error || Scene.CommitLoad() !== null)
                throw new Error('Commit failure must be queryable');
            Scene.CancelLoad(); Scene.CancelLoad();
            if (Scene.StepLoad(0) !== null || !Scene.BeginLoad('scenes/main.scene'))
                throw new Error('Canceled operation could not be restarted');
        } else if (this.phase === 2) {
            const progress = Scene.StepLoad(1);
            if (progress?.status === 'loading') return;
            if (progress?.status !== 'ready') throw new Error('New directory was not loaded');
            this.loaded = Scene.CommitLoad();
            if (!this.loaded?.FindNode('Prepared node') || this.old.IsValid() ||
                !this.loading.IsValid() || !this.loading.IsPersistent() ||
                this.old.SetPaused(false) || this.old.GetPaused() !== null ||
                this.loaded.GetPaused() !== false || Scene.StepLoad(0) !== null ||
                !this.loaded.SetPaused(true)) throw new Error('Prepared scene did not commit safely');
        } else if (this.phase === 3 && !Scene.BeginLoad('scenes/main.scene')) {
            throw new Error('Cannot prepare the operation canceled by Run cleanup');
        }
        Debug.Log('step-loading:' + this.phase++);
    }
}
)js";
        Fixture first;
        Fixture next;
        const auto advance_to = [&](std::string_view message)
        {
            for (int frame = 0; frame < 16 && !log_contains(message); ++frame)
            {
                if (!script.tick(.016) || !script.status().running)
                {
                    return false;
                }
            }
            return log_contains(message);
        };
        if (!script.initialize({}) ||
            !fixture(workspace, "step-first", {{"scripts/game.js", std::string(source)}}, first,
                     bytes) ||
            !fixture(workspace, "step-next", {{"scripts/game.js", std::string(source)}}, next,
                     bytes) ||
            !expect(apply(script, first) && world.paused() && advance_to("step-loading:0") &&
                        script.status().running,
                    "single-thread loading reaches ready without replacing the loading scene"))
        {
            return false;
        }
        const auto old_scene = world.scene()->id();
        if (!resources.update_catalog(next.catalog.data(), next.mounts) ||
            !expect(script.tick(.016) && script.status().running &&
                        world.scene()->id() == old_scene && world.paused() &&
                        log_contains("step-loading:1"),
                    "stale generation commit fails and exposes an error snapshot") ||
            !expect(advance_to("step-loading:2") && script.status().running &&
                        world.scene()->id() != old_scene && world.paused() &&
                        log_contains("step-loading:2") && script.tick(.016) &&
                        log_contains("step-loading:3"),
                    "fresh loading commits once and stale scene handles cannot change pause"))
        {
            return false;
        }
        script.stop_gameplay();
        return expect(!world.paused() && world.scene() == nullptr &&
                          !graph.find(guid_for("scenes/main.scene")),
                      "stopping a Run cancels pending loads and releases unshared resources");
    }();
    resources.set_loader(nullptr);
    return passed;
#else
    (void)workspace;
    return true;
#endif
}

bool run_storage_binding_cases(const fs::path& workspace)
{
    engine::plugins::script::ScriptSystem script;
    const auto directory = engine::App::persistent_data_path() / "userdata";
    const auto path = directory / "kv.json";
    constexpr std::string_view first_source = R"js(
import { Gameplay, Storage } from 'engine';
const topLevelBlocked = !Storage.isReady && Storage.lastError === null &&
    Storage.GetString('blocked') === null && !Storage.SetString('blocked', 'value') &&
    !Storage.HasKey('blocked') && !Storage.DeleteKey('blocked') && !Storage.Save();
function checkStorageArguments(kept) {
    const oversized = 'x'.repeat(4 * 1024 * 1024 + 1);
    const invalid = [
        [() => Storage.GetString(), null],
        [() => Storage.GetString('存档🔑', 'extra'), null],
        [() => Storage.GetString(true), null],
        [() => Storage.GetString(oversized), null],
        [() => Storage.SetString('存档🔑'), false],
        [() => Storage.SetString('存档🔑', 'changed', 'extra'), false],
        [() => Storage.SetString('存档🔑', 7), false],
        [() => Storage.SetString('存档🔑', oversized), false],
        [() => Storage.HasKey(), false],
        [() => Storage.HasKey('存档🔑', 'extra'), false],
        [() => Storage.HasKey(true), false],
        [() => Storage.HasKey(oversized), false],
        [() => Storage.DeleteKey(), false],
        [() => Storage.DeleteKey('存档🔑', 'extra'), false],
        [() => Storage.DeleteKey(true), false],
        [() => Storage.DeleteKey(oversized), false],
        [() => Storage.Save('extra'), false],
    ];
    for (const [call, expected] of invalid) {
        if (call() !== expected || typeof Storage.lastError !== 'string' ||
            Storage.lastError.length === 0) return false;
        const error = Storage.lastError;
        if (!Storage.isReady || Storage.lastError !== error ||
            Storage.GetString('存档🔑') !== kept || Storage.lastError !== '' ||
            Storage.GetString('missing') !== null || Storage.lastError !== '') return false;
    }
    return true;
}
export default class extends Gameplay {
    constructor() {
        super();
        this.constructorBlocked = !Storage.isReady && Storage.lastError === null &&
            Storage.GetString('存档🔑') === null && !Storage.SetString('blocked', 'value') &&
            !Storage.Save();
    }
    OnInitialize() {
        const value = JSON.stringify({ name: '玩家😀', coins: 7, items: ['剑', '盾'] });
        return topLevelBlocked && this.constructorBlocked && Object.isFrozen(Storage) && Storage.isReady &&
            Storage.lastError === '' && Storage.GetString('missing') === null &&
            Storage.lastError === '' && Storage.SetString('存档🔑', value) &&
            Storage.HasKey('存档🔑') && Storage.GetString('存档🔑') === value &&
            Storage.SetString('empty', '') && Storage.GetString('empty') === '' &&
            Storage.SetString('delete-me', 'value') && Storage.DeleteKey('delete-me') &&
            !Storage.HasKey('delete-me') && Storage.DeleteKey('missing') &&
            checkStorageArguments(value) && Storage.Save() && Storage.lastError === '' &&
            Storage.SetString('unsaved', 'discard this');
    }
    OnShutdown() {
        if (!Storage.SetString('shutdown-unsaved', 'discard this too'))
            throw new Error('Storage unavailable during shutdown');
    }
}
)js";
    constexpr std::string_view restored_source = R"js(
import { Gameplay, Storage, Debug } from 'engine';
export default class extends Gameplay {
    OnInitialize() {
        const shutdownValue = Storage.GetString('shutdown-saved');
        if (shutdownValue === 'saved by the old run') Debug.Log('storage:read-old-shutdown-save');
        const value = JSON.parse(Storage.GetString('存档🔑'));
        return Storage.isReady && Storage.lastError === '' && value.name === '玩家😀' &&
            value.coins === 7 && value.items.join(',') === '剑,盾' &&
            Storage.HasKey('empty') && Storage.GetString('empty') === '' &&
            !Storage.HasKey('delete-me') && Storage.GetString('unsaved') === null &&
            Storage.GetString('shutdown-unsaved') === null && Storage.lastError === '' &&
            (shutdownValue === null || shutdownValue === 'saved by the old run');
    }
    OnShutdown() {
        if (!Storage.SetString('shutdown-saved', 'saved by the old run') || !Storage.Save())
            throw new Error('Explicit shutdown save failed');
    }
}
)js";
    constexpr std::string_view failed_source = R"js(
import { Gameplay, Storage, Debug } from 'engine';
function unavailable() {
    return !Storage.isReady && typeof Storage.lastError === 'string' &&
        Storage.lastError.length > 0 && Storage.GetString('存档🔑') === null &&
        !Storage.SetString('overwrite', 'forbidden') && !Storage.HasKey('存档🔑') &&
        !Storage.DeleteKey('存档🔑') && !Storage.Save() && Storage.lastError.length > 0;
}
export default class extends Gameplay {
    OnInitialize() {
        if (!unavailable()) return false;
        const loadError = Storage.lastError;
        if (Storage.SetString('overwrite', 7) || Storage.lastError === loadError ||
            Storage.lastError.length === 0 || Storage.isReady) return false;
        return unavailable() && Storage.lastError === loadError;
    }
    Update() {
        if (!unavailable()) throw new Error('Failed storage retried within the same run');
        Debug.Log('storage:failed-run-stays-unavailable');
    }
}
)js";
    Fixture first;
    Fixture restored;
    Fixture failed;
    std::error_code error;
    if (!expect(!fs::exists(directory, error) && !error, "storage starts without user data") ||
        !script.initialize({}) ||
        !fixture(workspace, "storage-first", {{"scripts/game.js", std::string(first_source)}},
                 first) ||
        !fixture(workspace, "storage-restored", {{"scripts/game.js", std::string(restored_source)}},
                 restored) ||
        !fixture(workspace, "storage-failed", {{"scripts/game.js", std::string(failed_source)}},
                 failed) ||
        !expect(script.prepare_gameplay(first.catalog, first.mounts) &&
                    !fs::exists(directory, error) && !error,
                "storage precheck does not create or load user data") ||
        !engine::ResourceSystem::instance()->update_catalog(first.catalog.data(), first.mounts) ||
        !expect(script.start_prepared_gameplay(),
                "storage is callback-only and saves Unicode JSON, empty values and deletions"))
    {
        return false;
    }
    std::string saved;
    if (!expect(engine::ResourceSystem::read_text(path, saved) && !saved.empty(),
                "explicit Save writes native storage"))
    {
        return false;
    }
    script.stop_gameplay();
    std::string actual;
    if (!expect(script.status().last_error.empty() &&
                    engine::ResourceSystem::read_text(path, actual) && actual == saved,
                "stop and shutdown discard unsaved changes") ||
        !expect(apply(script, restored), "new run restores saved strings without unsaved keys") ||
        !expect(!log_contains("storage:read-old-shutdown-save") && apply(script, restored) &&
                    log_contains("storage:read-old-shutdown-save") &&
                    engine::ResourceSystem::read_text(path, saved),
                "prepared replacement first reads after the old run explicitly saves on shutdown"))
    {
        return false;
    }
    script.stop_gameplay();
    constexpr std::string_view corrupt = "{broken user data";
    if (!expect(engine::ResourceSystem::read_text(path, actual) && actual == saved &&
                    write_file(path, corrupt) && apply(script, failed) &&
                    engine::ResourceSystem::read_text(path, actual) && actual == corrupt,
                "corrupt storage reports failure and cannot be overwritten") ||
        !write_file(path, saved) ||
        !expect(script.tick(.01) && script.status().running &&
                    log_contains("storage:failed-run-stays-unavailable"),
                "failed storage load is not retried within an active run"))
    {
        return false;
    }
    script.stop_gameplay();
    if (!expect(engine::ResourceSystem::read_text(path, actual) && actual == saved &&
                    apply(script, restored),
                "a later run can load repaired storage"))
    {
        return false;
    }
    script.stop_gameplay();
    return expect(script.status().last_error.empty() &&
                      engine::ResourceSystem::read_text(path, actual) && actual == saved,
                  "repaired storage remains intact after an explicit shutdown save");
}

bool run_input_binding_cases(const fs::path& workspace)
{
    constexpr std::string_view missing_source = R"js(
import { Gameplay, Input } from 'engine';
const blocked = Input.GetDevices() === null && Input.CreateChannel() === null && Input.lastError === null;
export default class extends Gameplay {
    OnInitialize() {
        return blocked && Input.GetFrameState() === null && Input.CreateChannel() === null &&
            !Input.ConfigureChannel({slot:0,generation:1}, {actions:[]}) &&
            typeof Input.lastError === 'string' && Input.lastError.length > 0;
    }
}
)js";
    {
        engine::plugins::script::ScriptSystem script;
        Fixture missing;
        if (!script.initialize({}) ||
            !fixture(workspace, "input-missing", {{"scripts/game.js", std::string(missing_source)}},
                     missing) ||
            !expect(apply(script, missing), "input binding rejects an absent injected service"))
        {
            return false;
        }
    }
    engine::EngineConfig config;
    config.window_title = "Script input binding test";
    config.width = config.height = 64;
    engine::WindowSystem window;
    engine::InputSystem input(window);
    if (!expect(window.initialize(config) && input.initialize(config),
                "initialize input without a renderer"))
    {
        return false;
    }
    SDL_HideWindow(window.native_window());
    engine::schema::InputChannelHandle foreign;
    if (!input.create_channel(foreign))
    {
        return false;
    }
    const std::string source = R"js(
import { Gameplay, Debug, Input, InputDeviceKind, InputControlKind, InputActionKind,
    InputActivation, InputCaptureStatus } from 'engine';
const blocked = Input.GetDevices() === null && Input.CreateChannel() === null && Input.lastError === null;
export default class extends Gameplay {
    constructor() { super(); this.blocked = Input.CreateChannel() === null; this.phase = 0; }
    OnInitialize() {
        const foreign = {slot:)js" +
                               std::to_string(foreign.slot()) +
                               ",generation:" + std::to_string(foreign.generation()) + R"js(};
        const keyboard = Input.GetDevices()?.find(d => d.kind === InputDeviceKind.keyboard);
        const controls = keyboard && Input.GetControls(keyboard.connection_id);
        this.source = controls?.find(c => c.source.kind === InputControlKind.key && c.source.code === )js" +
                               std::to_string(SDL_SCANCODE_F24) + R"js()?.source;
        this.channel = Input.CreateChannel();
        if (!blocked || !this.blocked || !Object.isFrozen(Input) || !this.source || !this.channel ||
            typeof keyboard.connection_id !== 'bigint' || controls.length <= 6 ||
            !Input.ReadControl(this.source) || Input.GetContacts() === null ||
            Input.DestroyChannel(foreign) || Input.ConfigureChannel(foreign, {actions:[]}) ||
            Input.ReadAction(foreign, 'fire') !== null) return false;
        const data = {activation:InputActivation.use_current_state, actions:[{
            name:'fire', kind:InputActionKind.button, bindings:[{parts:[{source:this.source}]}]
        }]};
        if (!Input.ConfigureChannel(this.channel, data) ||
            Input.ReadAction(this.channel, 'fire') !== null ||
            Input.ConfigureChannel(this.channel, {...data, typo:true}) ||
            Input.ConfigureChannel(this.channel, {...data, priority:Infinity}) ||
            Input.ConfigureChannel(this.channel, {...data, activation:999}) ||
            Input.lastError.length === 0) return false;
        const old = Input.BeginCapture(this.channel, {devices:[keyboard.connection_id], kinds:[InputControlKind.key]});
        if (!old || !Input.CancelCapture(old)) return false;
        this.capture = Input.BeginCapture(this.channel, {devices:[keyboard.connection_id], kinds:[InputControlKind.key]});
        if (!this.capture || Input.CancelCapture(old) ||
            Input.ReadCapture(this.capture)?.status !== InputCaptureStatus.waiting) return false;
        Debug.Log('input-owned=' + JSON.stringify(this.channel));
        Debug.Log('input-capture=' + JSON.stringify(this.capture));
        return true;
    }
    Update() {
        const a = Input.ReadAction(this.channel, 'fire'), b = Input.ReadAction(this.channel, 'fire');
        const raw = Input.ReadControl(this.source), frame = Input.GetFrameState();
        if (!a || !b || !raw || !frame || a.frame_id !== frame.frame_id ||
            JSON.stringify(a, (_, v) => typeof v === 'bigint' ? v.toString() : v) !==
            JSON.stringify(b, (_, v) => typeof v === 'bigint' ? v.toString() : v))
            throw new Error('Input snapshot is unavailable or consuming');
        if (this.phase === 0 && (!a.held || !a.pressed || a.released || !raw.held ||
            Input.ReadCapture(this.capture)?.status !== InputCaptureStatus.completed))
            throw new Error('Input press or capture did not reach the script');
        if (this.phase === 1 && (a.held || a.pressed || !a.released || raw.held))
            throw new Error('Input release did not reach the script');
        Debug.Log('input-frame=' + this.phase++);
    }
}
)js";
    engine::plugins::script::ScriptSystem script(nullptr, nullptr, nullptr, nullptr, nullptr,
                                                 &input);
    Fixture first;
    if (!script.initialize(config) ||
        !fixture(workspace, "input-first", {{"scripts/game.js", source}}, first) ||
        !expect(apply(script, first),
                "input maps schema objects, protects ownership and replaces capture tokens"))
    {
        return false;
    }
    const auto logged_handle = []<typename Handle>(std::string_view marker, Handle& output)
    {
        auto& log = engine::LogSystem::instance();
        if (!log.flush())
        {
            return false;
        }
        std::ifstream file(log.file_path(), std::ios::binary);
        const std::string contents{std::istreambuf_iterator<char>(file),
                                   std::istreambuf_iterator<char>()};
        const auto found = contents.find(marker);
        const auto begin = found == std::string::npos ? found : contents.find('{', found);
        const auto end = begin == std::string::npos ? begin : contents.find('}', begin);
        if (!file.is_open() || file.bad() || end == std::string::npos)
        {
            return false;
        }
        const auto value = Json::parse(contents.substr(begin, end - begin + 1), nullptr, false);
        if (!value.is_object() || !value.contains("slot") || !value.contains("generation") ||
            !value["slot"].is_number_unsigned() || !value["generation"].is_number_unsigned())
        {
            return false;
        }
        output = {value["slot"].get<std::uint32_t>(), value["generation"].get<std::uint32_t>()};
        return true;
    };
    engine::schema::InputChannelHandle owned;
    engine::schema::InputCaptureHandle capture;
    if (!logged_handle("input-owned=", owned) || !logged_handle("input-capture=", capture))
    {
        return false;
    }
    for (int phase = 0; phase != 2; ++phase)
    {
        SDL_Event event{};
        event.type = phase == 0 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.windowID = SDL_GetWindowID(window.native_window());
        event.key.scancode = SDL_SCANCODE_F24;
        event.key.down = phase == 0;
        if (!expect(input.begin_frame() && input.set_routing_state({}) &&
                        input.process_event(event, false) && input.tick(.016) && script.tick(.016),
                    "published press and release snapshots cross the shared VM binding"))
        {
            return false;
        }
    }
    Fixture next;
    constexpr std::string_view next_source = R"js(
import { Gameplay, Input, Debug } from 'engine';
export default class extends Gameplay {
    OnInitialize() {
        const channel = Input.CreateChannel();
        if (!channel || !Input.ConfigureChannel(channel, {actions:[]})) return false;
        Debug.Log('input-pending=' + JSON.stringify(channel));
        return true;
    }
}
)js";
    if (!fixture(workspace, "input-next", {{"scripts/game.js", std::string(next_source)}}, next) ||
        !expect(apply(script, next), "input channels can be created by a replacement run"))
    {
        return false;
    }
    engine::schema::InputActionState old_action;
    engine::schema::InputCaptureStateT old_capture;
    if (!expect(!input.read_action(owned, "fire", old_action) &&
                    !input.read_capture(capture, old_capture) &&
                    input.set_channel_enabled(foreign, false,
                                              engine::schema::InputActivation::wait_for_neutral) &&
                    log_contains("input-frame=0") && log_contains("input-frame=1"),
                "Run replacement clears owned channels/captures without destroying host channels"))
    {
        return false;
    }
    engine::schema::InputChannelHandle pending;
    if (!logged_handle("input-pending=", pending))
    {
        return false;
    }
    script.stop_gameplay();
    return expect(script.status().last_error.empty() &&
                      !input.set_channel_enabled(
                          pending, false, engine::schema::InputActivation::wait_for_neutral) &&
                      input.destroy_channel(foreign),
                  "input Run cleanup also discards pending mappings");
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
    constexpr std::string_view source = R"js(
import { Gameplay, Render } from 'engine';
const blocked = !Render.SetVSync(false) && Render.GetVSync() === null &&
    Render.GetViewportSize() === null && !Render.SetMsaaSamples(4) &&
    Render.GetMsaaSamples() === null && !Render.SupportsMsaaSamples(4) &&
    !Render.SetMaxAnisotropy(true) && Render.GetMaxAnisotropy() === null &&
    !Render.SupportsMaxAnisotropy() && typeof Render.SetFeature === 'undefined' &&
    typeof Render.GetFeatureState === 'undefined' &&
    typeof Render.GetSubmissionStats === 'undefined' && typeof Render.DrawDebugLines === 'undefined';
export default class extends Gameplay {
    OnInitialize() {
        return blocked && Object.isFrozen(Render) &&
            Render.GetViewportSize() === null && Render.GetVSync() === null &&
            !Render.SetVSync(true) && !Render.SetVSync(1) &&
            !Render.SetMsaaSamples(4) && Render.GetMsaaSamples() === null &&
            !Render.SupportsMsaaSamples(1) && !Render.SupportsMsaaSamples(4) &&
            !Render.SetMsaaSamples('4') && !Render.SetMsaaSamples(2.5) &&
            !Render.SetMsaaSamples(0) && !Render.SetMsaaSamples(16) &&
            !Render.SetMaxAnisotropy(true) && !Render.SetMaxAnisotropy(1) &&
            Render.GetMaxAnisotropy() === null && !Render.SupportsMaxAnisotropy() &&
            typeof Render.SetFeature === 'undefined' &&
            typeof Render.GetFeatureState === 'undefined' &&
            typeof Render.GetSubmissionStats === 'undefined' &&
            typeof Render.DrawDebugLines === 'undefined' &&
            typeof Render.SetAmbient === 'undefined' &&
            typeof Render.SetExposure === 'undefined' &&
            typeof Render.SetDirectionalShadows === 'undefined';
    }
}
)js";
    Fixture input;
    if (!fixture(workspace, "render-device-controls", {{"scripts/game.js", std::string(source)}},
                 input))
    {
        return false;
    }
    {
        engine::plugins::script::ScriptSystem script;
        if (!script.initialize(config) ||
            !expect(apply(script, input), "device queries reject an absent render service"))
        {
            return false;
        }
    }
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows);
    engine::plugins::script::ScriptSystem script(nullptr, &renderer);
    return script.initialize(config) &&
           expect(apply(script, input),
                  "an uninitialized render service exposes no viewport or presentation controls");
}

bool run_scene_render_binding_cases(const fs::path& workspace)
{
    engine::WorldSystem world;
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows, &world);
    engine::plugins::script::ScriptSystem script(&world, &renderer);
    constexpr std::string_view source = R"js(
import { Gameplay, Scene, Camera, Light, Render,
         EnvironmentComponent, PostProcessComponent, PlanarReflectionComponent,
         LightProbeVolumeComponent, MeshRenderer } from 'engine';
export default class extends Gameplay {
    frame = 0;
    OnInitialize() {
        this.scene = Scene.Create('Scene environment');
        if (!this.scene) return false;
        const environmentNode = this.scene.CreateNode('Environment');
        this.environment = environmentNode.AddComponent(EnvironmentComponent);
        const cameraNode = this.scene.CreateNode('Camera');
        const camera = cameraNode.AddComponent(Camera);
        this.post = cameraNode.AddComponent(PostProcessComponent);
        const surfaceNode = this.scene.CreateNode('Reflector');
        const mesh = surfaceNode.AddComponent(MeshRenderer);
        const planar = surfaceNode.AddComponent(PlanarReflectionComponent);
        const probes = environmentNode.AddComponent(LightProbeVolumeComponent);
        if (!mesh || !planar || !probes || !planar.SetSettings(.75, .03) ||
            planar.SetSettings(.1, .03) || planar.SetSettings(.5, NaN) ||
            !mesh.SetBakeStatic(true) || !mesh.bakeStatic || !mesh.SetLightmap('') ||
            mesh.SetLightmap('missing.ktx') || !probes.SetResource('') ||
            probes.SetResource('missing.probes') ||
            !mesh.material.SetBlendMode('alpha_blend') ||
            mesh.material.GetBlendMode() !== 'alpha_blend' ||
            mesh.material.SetBlendMode('invalid')) return false;
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
            this.post.SetScreenSpaceReflections(true, .75, 96, 120, .2, .65) &&
            !this.post.SetScreenSpaceReflections(true, .1) &&
            !this.post.SetScreenSpaceReflections(true, .5, 64.5) &&
            !this.post.SetScreenSpaceReflections(true, .5, 257) &&
            !this.post.SetScreenSpaceReflections(true, .5, 64, NaN) &&
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
            light.SetDirectional() && light.SetShadowCascades(1, 100) &&
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
                    settings.exposure == 1.7F && settings.filmic_tonemap && settings.ao_enabled &&
                    settings.ao_radius == 2 && settings.ao_strength == 1.2F &&
                    settings.ao_power == 3 && settings.ao_fade_start == 20 &&
                    settings.ao_fade_end == 100 && settings.hdr_enabled && settings.bloom_enabled &&
                    settings.bloom_threshold == 1.2F && settings.bloom_intensity == .3F &&
                    settings.bloom_radius == 2 && settings.ssr_enabled &&
                    settings.ssr_max_steps == 96 && settings.ssr_resolution_scale == .75F &&
                    settings.ssr_max_distance == 120 && settings.ssr_thickness == .2F &&
                    settings.ssr_roughness_cutoff == .65F && settings.directional_shadows &&
                    settings.shadow_resolution == 1024 && settings.shadow_bias == .002F &&
                    settings.shadow_cascades == 2 && settings.shadow_distance == 90.0F &&
                    settings.shadow_split_lambda == .8F && settings.shadow_cascade_blend == .15F,
                "renderer reads component state; invalid setters preserve prior fields") ||
        !expect(
            script.tick(.016) && script.status().running &&
                renderer.settings().ambient_intensity == .4F &&
                !renderer.settings().ambient_tricolor && renderer.settings().exposure == .8F &&
                renderer.settings().ao_enabled && renderer.settings().ao_radius == 2 &&
                renderer.settings().ao_strength == 1.2F && renderer.settings().ao_power == 3 &&
                renderer.settings().ao_fade_start == 20 && renderer.settings().ao_fade_end == 100 &&
                renderer.settings().hdr_enabled && renderer.settings().bloom_enabled &&
                renderer.settings().bloom_threshold == 1.2F &&
                renderer.settings().bloom_intensity == .3F && renderer.settings().bloom_radius == 2,
            "live component changes affect the current scene") ||
        !expect(
            script.tick(.016) && script.status().running &&
                renderer.settings().ambient_intensity == 0.0F &&
                renderer.settings().exposure == 1.0F && !renderer.settings().ao_enabled &&
                renderer.settings().ao_radius == 1 && renderer.settings().ao_strength == 1 &&
                renderer.settings().ao_power == 1.5F && renderer.settings().ao_fade_start == 80 &&
                renderer.settings().ao_fade_end == 150 && !renderer.settings().hdr_enabled &&
                !renderer.settings().bloom_enabled && renderer.settings().bloom_threshold == 1 &&
                renderer.settings().bloom_intensity == .15F &&
                renderer.settings().bloom_radius == 1 && !renderer.settings().directional_shadows,
            "new scene does not inherit old environment or camera parameters"))
    {
        return false;
    }
    script.stop_gameplay();
    return expect(script.status().last_error.empty(), "scene render bindings shut down cleanly");
}

bool run_movie_binding_cases(const fs::path& workspace)
{
    using namespace engine;
    auto* resources = ResourceSystem::instance();
    ThreadSystem threads({2});
    TaskSystem tasks(threads, {"ScriptMovieTest", 2});
    AudioSystem audio;
    if (!resources || !threads.initialize({}) || !tasks.initialize({}) ||
        !resources->configure_async(tasks) || !audio.initialize({}))
    {
        return expect(false, "prepare movie binding services");
    }
    MoviePlayer player(*resources, tasks, audio);
    engine::plugins::script::ScriptSystem script(nullptr, nullptr, nullptr, nullptr, nullptr,
                                                  nullptr, nullptr, nullptr, &player);
    constexpr auto source = R"js(
import { Gameplay, Movie, MovieState, MovieStopReason } from 'engine';
const topBlocked = Movie.Play('movies/test.mp4') === null;
export default class extends Gameplay {
    OnInitialize() {
        if (!topBlocked) return false;
        const first = Movie.Play('movies/test.mp4');
        if (!first || typeof first.id !== 'bigint' || typeof first.generation !== 'bigint') return false;
        if (Movie.Status(first)?.state !== MovieState.preparing || Movie.SetPaused(first, true)) return false;
        if (Movie.Status({id:first.id, generation:first.generation + 1n}) !== null) return false;
        if (Movie.Play('movies/test.mp4') !== null || Movie.Release(first)) return false;
        if (!Movie.Stop(first, MovieStopReason.skipped) || Movie.Status(first)?.stopReason !== MovieStopReason.skipped) return false;
        if (!Movie.Release(first) || Movie.Status(first) !== null) return false;
        this.movie = Movie.Play('movies/test.mp4');
        return this.movie !== null;
    }
}
)js";
    Fixture candidate;
    if (!script.initialize({}) ||
        !fixture(workspace, "movie-binding", {{"scripts/game.js", source}}, candidate, {}, {}, {}, "placeholder") ||
        !apply(script, candidate))
    {
        return expect(false, "run movie facade from active gameplay");
    }
    MoviePlaybackHandle current;
    if (!expect(player.current_handle(current), "script owns accepted movie"))
    {
        return false;
    }
    script.stop_gameplay();
    const bool cleaned = expect(!player.current_handle(current), "run shutdown cancels and releases its movie");
    script.shutdown();
    resources->shutdown_async();
    return cleaned;
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
    app.assets_path = workspace / fs::path(u8"builtin-资源");
    app.persistent_data_path = workspace / fs::path(u8"data-持久");
    app.cache_path = workspace / fs::path(u8"cache-缓存");
    if (!engine::App::initialize(app) || !engine::App::game_language().empty())
    {
        return 1;
    }
    engine::App::set_game_language("zh-CN");
    const engine::EngineConfig config;
    if (!engine::LogSystem::instance().initialize(config))
    {
        engine::App::shutdown();
        return 1;
    }
    engine::ResourceSystem resources;
    const bool passed =
        resources.initialize(config) && run_cases(workspace) &&
        run_prepared_catalog_cases(workspace) && run_project_script_cases(workspace) &&
        run_module_contract_cases(workspace) && run_job_contract_cases(workspace) &&
        run_native_boundary_cases(workspace) && run_scene_cases(workspace) &&
        run_scene_query_cases(workspace) && run_component_identity_and_state_cases(workspace) &&
        run_persistent_scene_cases(workspace) && run_guid_cases(workspace) &&
        run_physics_audio_binding_cases(workspace) && run_impact_binding_cases(workspace) &&
        run_render_binding_cases(workspace) && run_scene_render_binding_cases(workspace) &&
        run_frame_rate_binding_cases(workspace) && run_json_binding_cases(workspace) &&
        run_storage_binding_cases(workspace) && run_input_binding_cases(workspace) &&
        run_runtime_control_cases(workspace) && run_step_loading_cases(workspace) &&
        run_movie_binding_cases(workspace) &&
        run_shooting_query_cases(workspace) && run_physics_interaction_binding_cases(workspace) &&
        run_physics_subscription_cases(workspace) &&
        run_physics_subscription_scene_cases(workspace) &&
        run_persistent_physics_subscription_cases(workspace) &&
        run_physics_subscription_promise_case(workspace);
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
