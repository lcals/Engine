#include "animation_fixture.hpp"

#include "plugins/runtime/script/script_system.hpp"
#include "runtime/app/app.hpp"
#include "runtime/log/log_system.hpp"

namespace
{
using namespace test_support::animation;

bool script_bindings(Fixture& fixture)
{
    constexpr std::string_view source = R"js(
import { Gameplay, Scene, Component, Animator, MeshRenderer } from 'engine';
export default class extends Gameplay {
    OnInitialize() {
        const scene = Scene.Create('Model and animation bindings');
        const root = scene?.InstantiateModel('project/models/character.fbx', 'Character');
        const body = scene?.FindNode('Body');
        if (!scene || !root || !body || !root.IsValid() ||
            scene.InstantiateModel('builtin/meshes/cube', 'Wrong type') !== null ||
            scene.InstantiateModel('project/missing.fbx', 'Missing') !== null ||
            !(body.GetComponent(MeshRenderer) instanceof MeshRenderer)) return false;
        const animator = body.AddComponent(Animator);
        if (!animator || !(animator instanceof Component) ||
            !(body.GetComponent(Animator) instanceof Animator) ||
            body.AddComponent(Animator) !== null || animator.Play() || animator.Pause() ||
            animator.SetSpeed(-1) || animator.SetSpeed(17) || animator.SetSpeed(NaN) ||
            animator.SetLoop(1) || animator.SetPlayOnAwake('yes') ||
            !animator.SetPlayOnAwake(false) || !animator.SetSpeed(2) || !animator.SetLoop(false) ||
            !animator.SetAnimation('project/animations/move.animation') ||
            animator.SetAnimation('builtin/meshes/cube') || animator.Seek(Infinity) ||
            animator.Seek(-1) || animator.Seek(2) || !animator.Play() || !animator.isPlaying ||
            animator.playback !== 'playing' || !animator.Pause() || animator.isPlaying ||
            animator.playback !== 'paused' || !animator.Seek(.5) || animator.time !== .5 ||
            !animator.Stop() || animator.time !== 0 || animator.playback !== 'stopped' ||
            !animator.Seek(.5) || animator.playback !== 'paused') return false;
        const temporary = scene.CreateNode('Temporary');
        const stale = temporary?.AddComponent(Animator);
        if (!stale || !temporary.Destroy() || stale.Play() || stale.Pause() || stale.Stop() ||
            stale.Seek(0) || stale.SetSpeed(1) || stale.isPlaying || stale.playback !== null ||
            stale.time !== null) return false;
        this.scene = scene;
        this.animator = animator;
        return true;
    }
    Update() {
        if (!this.scene.IsValid() || this.animator.time !== .5 || this.animator.isPlaying)
            throw new Error('Paused animation changed');
    }
}
)js";
    if (!fixture.initialize() || !add_model(fixture.files) ||
        !add_bytes(fixture.files, "40000000-0000-4000-8000-000000000008", "project/scripts/game.js",
                   engine::schema::AssetKind::script,
                   {reinterpret_cast<const std::byte*>(source.data()), source.size()}))
    {
        return false;
    }
    fixture.files.catalog.entries.back()->module_path = "scripts/game.js";
    if (!fixture.files.publish(fixture.resources))
    {
        return false;
    }
    fixture.world.destroy_scene();
    engine::plugins::script::ScriptSystem script(&fixture.world);
    if (!expect(script.initialize({}) && script.start_gameplay("scripts/game.js"),
                "real V8 executes model instantiation and Animator bindings") ||
        !expect(fixture.animation.tick(0) && script.tick(0.25) && script.status().running,
                "paused pose survives a script update"))
    {
        return false;
    }
    const auto* scene = fixture.world.scene();
    if (!expect(scene && scene->nodes().size() == 3 &&
                    close(scene->nodes()[2]
                              ->get_component<engine::MeshRenderer>()
                              ->deformed_vertices()[0]
                              .x(),
                          2),
                "script-created model and action drive actual ozz deformation"))
    {
        return false;
    }
    script.stop_gameplay();
    return expect(!fixture.world.scene() && fixture.animation.tick(0),
                  "stopping the gameplay releases the instantiated model hierarchy");
}
} // namespace

int main()
{
    Fixture fixture;
    engine::AppConfig app;
    app.application_name = "engine-model-animation-script-test";
    app.builtin_assets_path = fixture.files.root / "builtin";
    app.persistent_data_path = fixture.files.root / "persistent";
    app.cache_path = fixture.files.root / "cache";
    if (!engine::App::initialize(app) || !engine::LogSystem::instance().initialize({}))
    {
        engine::App::shutdown();
        return 1;
    }
    const bool passed = script_bindings(fixture);
    fixture.animation.shutdown();
    fixture.world.shutdown();
    fixture.resources.shutdown();
    engine::LogSystem::instance().shutdown();
    engine::App::shutdown();
    return passed ? 0 : 1;
}
