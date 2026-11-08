#include "resource_fixture.hpp"
#include "scene_snapshot.hpp"
#include "script_fixture.hpp"

#include "plugins/runtime/navigation_crowd/navigation_agent.hpp"
#include "plugins/runtime/script/script_system.hpp"
#include "runtime/app/app.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/navigation/navigation_system.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"
#include "runtime_plugins.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string_view>
#include <utility>

namespace
{

constexpr std::string_view source = R"js(
import { Gameplay, Scene, Component, NavigationAgent } from 'engine';
function check(value, message) { if (!value) throw new Error(message); }
export default class extends Gameplay {
    frame = 0;
    OnInitialize() {
        this.scene = Scene.Create('Navigation agent binding fixture');
        this.node = this.scene.CreateNode('Agent');
        this.old = this.node.AddComponent(NavigationAgent);
        check(this.old?.IsValid() && this.old.state === 'idle' && this.old.velocity?.x === 0,
              'new component must return initialized snapshots');
        check(Component.prototype.IsValid.call(this.old) &&
              Component.prototype.SetEnabled.call(this.old, false) &&
              !this.old.MoveTo({x:1,y:0,z:0}) && this.old.SetEnabled(true),
              'base methods must accept the live component instance');
        check(this.old.Configure({radius:.45,height:2,maxSpeed:4,maxAcceleration:12,
                                  separationWeight:3,arrivalDistance:.25,faceVelocity:false}),
              'valid complete configuration rejected');
        for (const invalid of [null, [], 1, {radius:-1}, {height:NaN}, {maxSpeed:'fast'},
                               {faceVelocity:1}, {radius:null}]) {
            check(!this.old.Configure(invalid), 'invalid configuration accepted');
        }
        check(this.old.MoveTo({x:5,y:0,z:3}) && this.old.state === 'pending',
              'accepted world target must be pending');
        check(!this.old.MoveTo({x:NaN,y:0,z:3}) && !this.old.MoveTo(null) &&
              this.old.state === 'pending', 'invalid target changed the active request');
        check(this.old.Stop() && this.old.state === 'idle' && this.old.MoveTo({x:5,y:0,z:3}),
              'stop and restart failed');
        return true;
    }
    Update() {
        if (this.frame++ === 0) {
            check(!this.old.IsValid() && !this.old.SetEnabled(false) &&
                  !this.old.Configure({}) && !this.old.MoveTo({x:1,y:0,z:0}) &&
                  !this.old.Stop() && this.old.state === null && this.old.velocity === null,
                  'removed component handle revived after replacement on the same node');
            check(!Component.prototype.IsValid.call(this.old) &&
                  !Component.prototype.SetEnabled.call(this.old, false),
                  'explicit base methods bypassed the expired component identity');
            this.current = this.node.GetComponent(NavigationAgent);
            check(this.current?.IsValid() && this.current.Configure({}) &&
                  this.current.MoveTo({x:2,y:0,z:0}) && this.current.state === 'pending',
                  'replacement handle or default configuration failed');
            check(this.current.SetEnabled(false) && !this.current.MoveTo({x:3,y:0,z:0}) &&
                  this.current.Stop() && this.current.state === 'idle' && this.current.SetEnabled(true),
                  'component enable and stop contract failed');
            return;
        }
        check(this.current.state === 'invalid' && this.current.velocity?.x === 0,
              'native crowd state must reach the script snapshot');
        check(this.scene.Destroy() && !this.current.IsValid() && this.current.state === null &&
              this.current.velocity === null && !this.current.Stop(),
              'destroyed scene retained live navigation handles');
        const scene = Scene.Create('Replacement scene');
        const fresh = scene.CreateNode('Agent').AddComponent(NavigationAgent);
        check(fresh?.IsValid() && !this.current.SetEnabled(false) && fresh.MoveTo({x:1,y:0,z:0}),
              'old scene handle must not affect a new component');
    }
}
)js";

bool expect(bool condition, const char* message,
            const engine::plugins::script::ScriptSystem& script)
{
    if (!condition)
    {
        std::fprintf(stderr, "navigation agent script test: %s (%s)\n", message,
                     script.status().last_error.c_str());
    }
    return condition;
}

bool add_script(test_support::ResourceFixture& files)
{
    auto entry = std::make_unique<engine::schema::AssetEntryT>();
    entry->guid = "50000000-0000-4000-8000-000000000011";
    entry->path = "scripts/game.js";
    entry->module_path = entry->path;
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
    using engine::plugins::navigation_crowd::NavigationAgent;
    engine::ThreadSystem threads({.worker_limit = 2});
    test_support::ResourceServices services(&threads);
    auto& resources = services.core;

    engine::TaskSystem tasks(threads, {.name = "Navigation binding tests", .max_concurrency = 2});
    engine::WorldSystem world(&services.loader);
    auto navigation = std::make_unique<engine::NavigationSystem>();
    auto* navigation_ptr = navigation.get();
    engine::WindowSystem window;
    engine::PhysicsSystem physics(&world);
    engine::RenderSystem renderer(window, &world);
    engine::plugins::script::ScriptSystem script(&services.loader, &world, nullptr, nullptr,
                                                 nullptr, navigation_ptr);
    std::unique_ptr<engine::System> navigation_base = std::move(navigation);
    if (!expect((threads.initialize({}) && services.initialize({})) && tasks.initialize({}) &&
                    world.add_subsystem(navigation_base) &&
                    configure_runtime_plugins(engine::RuntimeStage::before_physics, world, window,
                                              physics, *navigation_ptr, renderer, tasks) &&
                    world.subsystem_count() == 2 &&
                    configure_runtime_plugins(engine::RuntimeStage::after_physics, world, window,
                                              physics, *navigation_ptr, renderer, tasks) &&
                    world.subsystem_count() == 2 && world.initialize({}) &&
                    world.component_registry().contains(NavigationAgent::component_type) &&
                    script.initialize({}) && add_script(files) && files.publish(resources) &&
                    test_support::start_script(script, services, {{"content", files.root}},
                                               "scripts/game.js"),
                "production host assembly registers the plugin without a window or GPU", script))
    {
        return false;
    }
    auto* scene = world.scene();
    auto node = scene != nullptr && !test_support::scene_nodes(*scene).empty()
                    ? test_support::scene_nodes(*scene).front()
                    : engine::Node{};
    auto old_agent = node.valid() ? node.get_component<NavigationAgent>() : NavigationAgent{};
    if (!expect(old_agent.valid() &&
                    std::abs(test_support::read_settings(old_agent).radius - .45F) < .0001F &&
                    test_support::read_settings(old_agent).max_speed == 4.0F &&
                    !test_support::read_settings(old_agent).face_velocity &&
                    old_agent.state() == engine::NavigationAgentStatus::pending,
                "script configuration and pending target use the native component", script))
    {
        return false;
    }
    const auto old_identity = old_agent.instance_id();
    if (!expect(node.remove_component(NavigationAgent::component_type),
                "remove the native component while retaining its script handle", script))
    {
        return false;
    }
    auto fresh = node.add_component<NavigationAgent>();
    if (!expect(fresh.valid() && fresh.instance_id() != old_identity && script.tick(.016) &&
                    script.status().running && fresh.enabled() &&
                    test_support::read_settings(fresh) == engine::schema::NavigationAgentDataT{} &&
                    fresh.state() == engine::NavigationAgentStatus::idle,
                "stale handle is rejected and the replacement uses schema defaults", script) ||
        !expect(world.tick(.016) && script.tick(.016) && script.status().running,
                "crowd state queries and cross-scene invalidation use the actual script backend",
                script))
    {
        return false;
    }
    script.stop_gameplay();
    return expect(!world.scene() && !script.status().running && script.status().last_error.empty(),
                  "stopping the script releases the scene and component instances", script);
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
    app.application_name = "engine-navigation-agent-script-test";
    app.assets_path = files.root / "builtin";
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
