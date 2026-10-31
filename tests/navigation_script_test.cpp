#include "resource_fixture.hpp"

#include "plugins/runtime/script/script_system.hpp"
#include "runtime/app/app.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/navigation/navigation_system.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

constexpr std::string_view source = R"js(
import { Gameplay, Scene, Transform, MeshRenderer, Navigation } from 'engine';
const minimum = {x:12,y:-2,z:23}, maximum = {x:36,y:4,z:39};
const options = {minimum,maximum,tileSize:16};
const start = {x:14,y:0,z:31}, end = {x:34,y:0,z:31};
function check(value, message) { if (!value) throw new Error(message); }
function close(a,b) { return Math.abs(a-b) < .001; }
function reachable() {
    const path = Navigation.FindPath(start,end);
    return Array.isArray(path) && path.length >= 2 && close(path[0].x,start.x) &&
        close(path[path.length-1].x,end.x);
}
const topLevelBlocked = Navigation.ready === false && Navigation.revision === null &&
    !Navigation.Build([],options) && Navigation.FindPath(start,end) === null;
export default class extends Gameplay {
    frame = 0;
    constructor() {
        super();
        this.constructorBlocked = !Navigation.Clear() && Navigation.ready === false;
    }
    createGround() {
        this.scene = Scene.Create('Navigation script fixture');
        check(this.scene && !Navigation.ready, 'fresh scene inherited old navigation');
        const parent = this.scene.CreateNode('Parent');
        this.floor = this.scene.CreateNode('Floor');
        check(parent && this.floor && this.floor.SetParent(parent), 'create hierarchy');
        check(parent.GetComponent(Transform).SetPosition(10,0,20), 'set parent position');
        this.transform = this.floor.GetComponent(Transform);
        check(this.transform.SetPosition(2,0,3), 'set child position');
        const position = this.transform.GetWorldPosition();
        check(position && close(position.x,12) && close(position.y,0) && close(position.z,23),
              'GetWorldPosition must include the parent transform');
        const facing = this.scene.CreateNode('Facing');
        const facingTransform = facing?.GetComponent(Transform);
        check(facing && facing.SetParent(parent) && facingTransform.SetRotation(0,90,0) &&
              facingTransform.SetScale(2,3,4), 'set facing transform');
        const forward = facingTransform.GetWorldForward();
        check(forward && close(forward.x,-1) && close(forward.y,0) && close(forward.z,0),
              'GetWorldForward must be rotated and normalized after scaling');
        this.renderer = this.floor.AddComponent(MeshRenderer);
        check(this.renderer && this.renderer.SetMesh('project/meshes/floor.mesh'), 'load floor mesh');
    }
    build() {
        check(Navigation.Build([this.floor],options) && Navigation.ready && reachable(),
              'explicit transformed mesh did not produce a path: '+Navigation.lastError);
        check(typeof Navigation.revision === 'bigint' && Navigation.rebuiltTiles === 24,
              'navigation statistics have incorrect script types or values');
    }
    rejected(callback, message) {
        const revision = Navigation.revision;
        check(callback() === false && !Navigation.ready && Navigation.revision > revision &&
              Navigation.FindPath(start,end) === null, message);
        this.build();
    }
    OnInitialize() {
        check(topLevelBlocked && this.constructorBlocked, 'navigation escaped the Run call boundary');
        this.createGround();
        check(Navigation.Build([],options) && Navigation.ready &&
              Navigation.FindPath(start,end) === null, 'Build must not scan unsubmitted scene nodes');
        this.build();
        return true;
    }
    Update() {
        if (this.frame++ === 0) {
            const dirtyMin={x:22,y:-1,z:23}, dirtyMax={x:26,y:1,z:39};
            const revision=Navigation.revision;
            check(Navigation.Replace([],dirtyMin,dirtyMax) && Navigation.ready &&
                  Navigation.revision > revision && Navigation.rebuiltTiles > 0 &&
                  Navigation.rebuiltTiles < 24 && Navigation.FindPath(start,end) === null,
                  'local removal must sever the connection without rebuilding every tile');
            check(Navigation.Replace([this.floor],dirtyMin,dirtyMax) && reachable(),
                  'local restoration must reconnect tiles');
            this.rejected(() => Navigation.Replace([null],minimum,maximum),
                          'invalid JS node bypassed native failure cleanup');
            this.rejected(() => Navigation.Replace([this.floor,this.floor],minimum,maximum),
                          'duplicate native node input retained old navigation');
            this.rejected(() => Navigation.Replace([this.floor],{x:NaN,y:0,z:23},maximum),
                          'invalid dirty coordinates retained old navigation');
            this.rejected(() => Navigation.Build([this.floor],{...options,cellSize:'bad'}),
                          'invalid build scalar retained old navigation');
            check(this.renderer.SetEnabled(false), 'disable submitted renderer');
            check(!Navigation.Replace([this.floor],minimum,maximum) && !Navigation.ready,
                  'disabled renderer was accepted or retained stale navigation');
            check(this.renderer.SetEnabled(true), 'restore renderer');
            this.build();
            return;
        }
        if (this.frame === 2) {
            this.oldFloor=this.floor;
            this.oldTransform=this.transform;
            check(this.scene.Destroy() && !Navigation.ready &&
                  this.oldTransform.GetWorldPosition() === null &&
                  this.oldTransform.GetWorldForward() === null,
                  'destroyed scene retained navigation or usable transform handles');
            return;
        }
        this.createGround();
        this.build();
        this.rejected(() => Navigation.Replace([this.oldFloor],minimum,maximum),
                      'previous-scene Node handle was accepted');
    }
}
)js";

bool expect(bool condition, const char* message,
            const engine::plugins::script::ScriptSystem& script)
{
    if (!condition)
    {
        std::fprintf(stderr, "navigation script test: %s (%s)\n", message,
                     script.status().last_error.c_str());
    }
    return condition;
}

bool add_script(test_support::ResourceFixture& files)
{
    auto entry = std::make_unique<engine::schema::AssetEntryT>();
    entry->guid = "50000000-0000-4000-8000-000000000001";
    entry->path = "project/scripts/game.js";
    entry->module_path = "scripts/game.js";
    entry->kind = engine::schema::AssetKind::script;
    engine::ByteBuffer entity;
    if (!engine::ResourceSystem::encode_resource(
            entry->guid, entry->kind,
            {reinterpret_cast<const std::byte*>(source.data()), source.size()}, entity,
            entry->header_size))
    {
        return false;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto value : entity)
    {
        hash = (hash ^ std::to_integer<std::uint8_t>(value)) * 1099511628211ULL;
    }
    char digest[17]{};
    std::snprintf(digest, sizeof(digest), "%016llx", static_cast<unsigned long long>(hash));
    entry->content_hash = digest;
    entry->content_size = entity.size();
    entry->location = std::make_unique<engine::schema::FileLocationT>();
    entry->location->mount = "content";
    entry->location->path = "gen/" + entry->guid;
    entry->location->length = entity.size();
    std::ofstream output(files.root / entry->location->path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(entity.data()),
                 static_cast<std::streamsize>(entity.size()));
    output.close();
    if (!output)
    {
        return false;
    }
    files.catalog.entries.push_back(std::move(entry));
    return true;
}

bool run(test_support::ResourceFixture& files)
{
    engine::ResourceSystem resources;
    engine::WorldSystem world;
    engine::NavigationSystem navigation;
    engine::plugins::script::ScriptSystem script(&world, nullptr, nullptr, nullptr, &navigation);
    engine::schema::MeshDataT floor;
    floor.path = "project/meshes/floor.mesh";
    floor.vertices = {
        {0, 0, 0, 0, 1, 0}, {24, 0, 0, 0, 1, 0}, {0, 0, 16, 0, 1, 0}, {24, 0, 16, 0, 1, 0}};
    floor.indices = {0, 2, 1, 1, 2, 3};
    if (!expect(resources.initialize({}) && world.initialize({}) && navigation.initialize({}) &&
                    script.initialize({}) && files.add(floor) && add_script(files) &&
                    files.publish(resources) && script.start_gameplay("scripts/game.js"),
                "initialize actual script backend and explicitly submitted navigation", script))
    {
        return false;
    }
    std::vector<engine::Vec3> path;
    const auto revision = navigation.revision();
    if (!expect(navigation.ready() && world.scene() &&
                    navigation.find_path({14, 0, 31}, {34, 0, 31}, path) && script.tick(.016) &&
                    script.status().running && navigation.ready() &&
                    navigation.revision() > revision,
                "script local rebuild and rejected inputs preserve the failure contract", script))
    {
        return false;
    }
    const auto before_destroy = navigation.revision();
    if (!expect(script.tick(.016) && script.status().running && !world.scene() &&
                    !navigation.ready() && navigation.revision() > before_destroy,
                "destroying the script scene clears actual native navigation", script) ||
        !expect(script.tick(.016) && script.status().running && world.scene() &&
                    navigation.ready() && navigation.find_path({14, 0, 31}, {34, 0, 31}, path),
                "new scene can rebuild and rejects handles from the previous scene", script))
    {
        return false;
    }
    const auto before_stop = navigation.revision();
    script.stop_gameplay();
    const auto stopped = navigation.revision();
    script.stop_gameplay();
    return expect(!world.scene() && !navigation.ready() && !script.status().running &&
                      script.status().last_error.empty() && stopped > before_stop &&
                      navigation.revision() == stopped,
                  "stopping releases navigation and repeating stop is harmless", script);
}

} // namespace

int main()
{
    test_support::ResourceFixture files;
    if (files.root.empty())
    {
        return 1;
    }
    engine::AppConfig app;
    app.application_name = "engine-navigation-script-test";
    app.builtin_assets_path = files.root / "builtin";
    app.persistent_data_path = files.root / "persistent";
    app.cache_path = files.root / "cache";
    if (!engine::App::initialize(app) || !engine::LogSystem::instance().initialize({}))
    {
        engine::App::shutdown();
        return 1;
    }
    const bool passed = run(files);
    engine::LogSystem::instance().shutdown();
    engine::App::shutdown();
    return passed ? 0 : 1;
}
