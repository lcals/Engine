#include "animation_fixture.hpp"
#include "scene_snapshot.hpp"
#include "script_fixture.hpp"

#include "plugins/runtime/script/script_system.hpp"
#include "runtime/app/app.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/schema/prefab_generated.h"

namespace
{
using namespace test_support::animation;

bool add_prefab(Fixture& fixture)
{
    engine::schema::PrefabDataT prefab;
    prefab.schema_version = 3;
    prefab.root_id = 1;
    auto node = std::make_unique<engine::schema::NodeDataT>();
    node->info = test_support::node_info();
    node->info->properties->id = 1;
    node->info->properties->name = "Template";
    auto transform = test_support::component_data();
    transform->payload.Set(engine::schema::TransformDataT{});
    node->components.push_back(std::move(transform));
    prefab.nodes.push_back(std::move(node));
    flatbuffers::FlatBufferBuilder builder;
    engine::schema::FinishPrefabDataBuffer(builder,
                                           engine::schema::PrefabData::Pack(builder, &prefab));
    return add(fixture.files, "40000000-0000-4000-8000-000000000009", "prefabs/template.prefab",
               engine::schema::AssetKind::prefab, builder);
}

bool script_bindings(Fixture& fixture)
{
    constexpr std::string_view source = R"js(
import { Gameplay, Scene, Component, Animator, MeshRenderer } from 'engine';
export default class extends Gameplay {
    OnInitialize() {
        const scene = Scene.Create('Model and animation bindings');
        const first = scene?.InstantiatePrefab('prefabs/template.prefab', 'First');
        const second = scene?.InstantiatePrefab('prefabs/template.prefab', 'Second');
        if (!first || !second || !first.SetEnabled(false) || !second.active ||
            scene.FindNode('First')?.id !== first.id ||
            scene.InstantiatePrefab('missing.prefab', 'Missing') !== null ||
            scene.InstantiatePrefab('models/character.fbx', 'Wrong type') !== null ||
            !first.Destroy() || first.IsValid() || !second.IsValid() || !second.Destroy())
            return false;
        const root = scene?.InstantiateModel('models/character.fbx', 'Character');
        const otherRoot = scene?.InstantiateModel('models/character.fbx', 'Character');
        const body = root?.FindChild('Assembly/Body');
        const otherBody = otherRoot?.FindChild('Assembly/Body');
        if (!scene || !root || !otherRoot || !body || !otherBody || !root.IsValid() ||
            scene.InstantiateModel('builtin://meshes/cube', 'Wrong type') !== null ||
            scene.InstantiateModel('missing.fbx', 'Missing') !== null ||
            !(body.GetComponent(MeshRenderer) instanceof MeshRenderer)) return false;
        for (const path of ['', '.', '..', '/Assembly', 'Assembly/', 'Assembly//Body',
                            'Assembly/./Body', 'Assembly/../Body', 'Body', 'Missing/Body', 3, null]) {
            if (root.FindChild(path) !== null) return false;
        }
        const assembly = root.FindChild('Assembly');
        const duplicate = scene.CreateNode('Assembly');
        const duplicateBody = scene.CreateNode('Body');
        if (!assembly || !duplicate || !duplicateBody || !duplicate.SetParent(root) ||
            root.FindChild('Assembly') !== null || root.FindChild('Assembly/Body') !== null ||
            !otherRoot.FindChild('Assembly/Body') || !duplicate.Destroy() ||
            !duplicateBody.SetParent(assembly) || root.FindChild('Assembly/Body') !== null ||
            !duplicateBody.Destroy() || !root.FindChild('Assembly/Body')) return false;
        const animator = body.AddComponent(Animator);
        if (!animator || !(animator instanceof Component) ||
            !(body.GetComponent(Animator) instanceof Animator) ||
            body.AddComponent(Animator) !== null || animator.Play() || animator.Pause() ||
            animator.SetSpeed(-1) || animator.SetSpeed(17) || animator.SetSpeed(NaN) ||
            animator.SetLoop(1) || animator.SetPlayOnAwake('yes') ||
            !animator.SetPlayOnAwake(false) || !animator.SetSpeed(2) || !animator.SetLoop(false) ||
            !animator.SetAnimation('animations/move.animation') ||
            animator.CrossFade('animations/scale.animation', -1) ||
            animator.CrossFade('missing.animation', .2) ||
            animator.CrossFade('animations/scale.animation', 'bad') ||
            !animator.CrossFade('animations/scale.animation', .2) ||
            !animator.CrossFade('animations/move.animation', 0) ||
            animator.SetAnimation('builtin://meshes/cube') || animator.Seek(Infinity) ||
            animator.Seek(-1) || animator.Seek(2) || !animator.Play() || !animator.isPlaying ||
            animator.playback !== 'playing' || !animator.Pause() || animator.isPlaying ||
            animator.playback !== 'paused' || !animator.Seek(.5) || animator.time !== .5 ||
            !animator.Stop() || animator.time !== 0 || animator.playback !== 'stopped' ||
            !animator.Seek(.5) || animator.playback !== 'paused') return false;
        if (otherBody.GetComponent(Animator) !== null) return false;
        const otherAnimator = otherBody.AddComponent(Animator);
        if (!otherAnimator || !otherAnimator.SetPlayOnAwake(false) ||
            !otherAnimator.SetAnimation('animations/move.animation') ||
            !otherAnimator.Seek(.25) || animator.time !== .5) return false;
        const temporary = scene.CreateNode('Temporary');
        const stale = temporary?.AddComponent(Animator);
        if (!stale || !temporary.Destroy() || stale.Play() || stale.Pause() || stale.Stop() ||
            stale.CrossFade('animations/move.animation', .2) ||
            temporary.FindChild('Body') !== null ||
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
    if (!fixture.initialize() || !add_model(fixture.files) || !add_prefab(fixture) ||
        !add_bytes(fixture.files, "40000000-0000-4000-8000-000000000008", "scripts/game.js",
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
    engine::plugins::script::ScriptSystem script(&fixture.services.loader, &fixture.world);
    if (!expect(script.initialize({}) && test_support::start_script(
                                             script, fixture.services,
                                             {{"content", fixture.files.root}}, "scripts/game.js"),
                "selected script backend executes model instantiation and Animator bindings") ||
        !expect(fixture.animation.tick(0) && script.tick(0.25) && script.status().running,
                "paused pose survives a script update"))
    {
        return false;
    }
    const auto* scene = fixture.world.scene();
    if (!expect(scene && test_support::scene_nodes(*scene).size() == 6 &&
                    close(sampled_vertex(test_support::scene_nodes(*scene)[2]
                                             .get_component<engine::MeshRenderer>())
                              .x(),
                          2) &&
                    close(sampled_vertex(test_support::scene_nodes(*scene)[5]
                                             .get_component<engine::MeshRenderer>())
                              .x(),
                          1),
                "relative lookup configures two identical model instances independently"))
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
    app.assets_path = fixture.files.root / "builtin";
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
