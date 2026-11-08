#include "animation_fixture.hpp"
#include "script_fixture.hpp"

#include "plugins/runtime/script/script_system.hpp"
#include "runtime/app/app.hpp"
#include "runtime/log/log_system.hpp"

#include <string>
#include <string_view>

namespace
{
using namespace test_support::animation;

bool load_script(Fixture& fixture, engine::plugins::script::ScriptSystem& script,
                 std::string_view source, unsigned sequence)
{
    char guid[37]{};
    std::snprintf(guid, sizeof(guid), "40000000-0000-4000-8000-%012u", 100 + sequence);
    const auto path = "scripts/state-machine-" + std::to_string(sequence) + ".js";
    if (!add_bytes(fixture.files, guid, path, engine::schema::AssetKind::script,
                   {reinterpret_cast<const std::byte*>(source.data()), source.size()}))
    {
        return false;
    }
    fixture.files.catalog.entries.back()->module_path = path;
    fixture.world.destroy_scene();
    return fixture.files.publish(fixture.resources) && script.initialize({}) &&
           test_support::start_script(script, fixture.services, {{"content", fixture.files.root}},
                                      path);
}

bool check(test_support::ResourceServices& services, engine::plugins::script::ScriptSystem& script,
           std::string_view body, const char* message)
{
    std::string source = "const {Animator}=require('engine'); exports.default=()=>{";
    source += body;
    source += "};";
    std::string result;
    std::string error;
    const bool passed = test_support::execute_script(
        services, script, {script.status().run_id, "scripts/fsm-check.js", source, "null"}, result,
        error);
    if (!passed || result != "true")
    {
        std::fprintf(stderr, "FSM check failed: %s; result=%s, error=%s\n", message, result.c_str(),
                     error.c_str());
    }
    return expect(passed && result == "true", message);
}

bool shared_instances(Fixture& fixture)
{
    constexpr std::string_view source = R"js(
import {Gameplay, Scene, Animator, AnimationStateMachine, AnimationState,
        AnimationCondition} from 'engine';
const f = globalThis.fsm = {actors: [], kept: {}, failed: {}, updates: {}, saved: null, builds: 0};
class State extends AnimationState {
    OnUpdate(context) {
        const id = context.GetInt('id');
        f.updates[id] = (f.updates[id] || 0) + 1;
        f.kept[id] = context.IsTriggered('keep');
        f.failed[id] = context.IsTriggered('fail');
        if (id === 1) f.saved = context;
        if (!context.SetSpeed(context.GetFloat('speed'))) throw Error('state playback speed');
    }
}
class Hit extends AnimationCondition {
    Test(context) {
        if (context.SetSpeed(4) !== false) throw Error('condition modified playback');
        return context.GetBool('allow') === true && context.IsTriggered('hit');
    }
}
class Definition extends AnimationStateMachine {
    constructor() {
        super('Idle'); ++f.builds;
        for (const [name, type, value] of [['id','int',0], ['speed','float',1],
            ['allow','bool',true], ['hit','trigger',false], ['keep','trigger',false],
            ['fail','trigger',false], ['reenter','trigger',false]]) {
            if (!this.AddParameter(name,type,value)) throw Error('parameter declaration');
        }
        this.AddState(new State('Idle','idle'));
        this.AddState(new State('Fire','fire',{loop:false}));
        this.AddState(new State('Missing','missing'));
        this.AddTransition('Idle','Fire', c => c.IsTriggered('keep') && false, {priority:100});
        this.AddTransition('Idle','Missing', c => c.IsTriggered('fail'), {priority:20});
        this.AddTransition('Idle','Fire',new Hit(),{priority:10,seconds:.5});
        this.AddTransition('Fire','Fire',c => c.IsTriggered('reenter'),
            {priority:10,seconds:.2,interrupt:true,reenter:true});
        this.AddTransition('Fire','Idle',c => c.finished,{exitTime:1,seconds:.25});
    }
}
f.definition = new Definition();
f.aliases = {idle:'animations/move.animation',
    fire:{path:'animations/scale.animation'},missing:'animations/missing.animation'};
export default class extends Gameplay {
    OnInitialize() {
        f.scene = Scene.Create('Shared animation state machines');
        if (!f.scene) return false;
        for (let id=1; id<=8; ++id) {
            const root = f.scene.InstantiateModel('models/character.fbx','Actor'+id);
            const animator = root?.FindChild('Assembly/Body')?.AddComponent(Animator);
            if (!animator?.SetStateMachine(f.definition,f.aliases) ||
                !animator.SetParameters({id,speed:1})) return false;
            f.actors.push(animator);
        }
        return f.builds === 1;
    }
}
)js";
    engine::plugins::script::ScriptSystem script(&fixture.services.loader, &fixture.world);
    if (!expect(load_script(fixture, script, source, 1),
                "one immutable definition binds eight independent actors") ||
        !check(fixture.services, script, R"js(
const f=globalThis.fsm, a=f.actors[0];
return f.builds===1 && f.actors.every(x=>x.stateMachineState==='Idle') &&
    !f.definition.AddParameter('late','bool') &&
    a.SetStateMachine(f.definition,f.aliases) &&
    a.SetParameters({speed:2}) && !a.SetParameters({speed:3,id:1.5}) &&
    !a.SetParameters({unknown:1}) && !a.SetParameters({hit:true}) &&
    !a.Trigger('unknown') && !a.CrossFade('animations/scale.animation',0);
)js",
               "component controls are exclusive and parameter batches validate atomically") ||
        !expect(script.tick(.25) && fixture.animation.tick(.25), "advance shared machines") ||
        !check(fixture.services, script, R"js(
const f=globalThis.fsm;
let expired=false; try { void f.saved.state; } catch(e) { expired=e instanceof TypeError; }
return expired && Math.abs(f.actors[0].time-.5)<.001 &&
    Math.abs(f.actors[1].time-.25)<.001 &&
    f.actors[0].node.GetComponent(Animator).stateMachineState==='Idle' &&
    f.actors[0].Trigger('keep') && f.actors[0].Trigger('hit');
)js",
               "context expires and per-instance parameters do not contaminate shared state") ||
        !expect(script.tick(0) && fixture.animation.tick(0), "commit triggered transition") ||
        !check(fixture.services, script, R"js(
const f=globalThis.fsm;
return f.actors[0].stateMachineState==='Fire' && f.actors[0].stateMachineTransitioning &&
    f.actors[1].stateMachineState==='Idle' && f.kept[1]===true &&
    f.actors[0].Trigger('hit');
)js",
               "an unselected condition cannot consume another trigger or affect another actor") ||
        !expect(script.tick(.25) && fixture.animation.tick(.25), "advance an active fade") ||
        !check(fixture.services, script, "return globalThis.fsm.actors[0].Trigger('reenter');",
               "request same-state reentry while a transition is active") ||
        !expect(script.tick(0) && fixture.animation.tick(0), "interrupt from the displayed pose") ||
        !check(fixture.services, script, R"js(
const f=globalThis.fsm,a=f.actors[0];
f.beforePause=f.updates[1];
return a.stateMachineState==='Fire' && a.time===0 && a.SetStateMachinePaused(true);
)js",
               "reentry restarts the same clip atomically") ||
        !expect(script.tick(1) && fixture.animation.tick(1), "tick with one paused machine") ||
        !check(fixture.services, script, R"js(
const f=globalThis.fsm,a=f.actors[0];
return a.time===0 && f.updates[1]===f.beforePause && a.stateMachinePaused===true &&
    a.SetStateMachinePaused(false) && f.scene.SetPaused(true);
)js",
               "pausing one machine freezes its callbacks and native clock") ||
        !expect(script.tick(1) && fixture.animation.tick(1), "tick paused world") ||
        !check(fixture.services, script, R"js(
const f=globalThis.fsm;
return f.actors[0].time===0 && f.updates[1]===f.beforePause && f.scene.SetPaused(false);
)js",
               "world pause leaves menu updates alive without advancing machines") ||
        !expect(script.tick(.5) && fixture.animation.tick(.5), "finish a nonlooping action") ||
        !check(fixture.services, script, "return globalThis.fsm.actors[0].playback==='paused';",
               "native completion retains the nonlooping final pose") ||
        !expect(script.tick(0) && fixture.animation.tick(0), "evaluate completion while paused") ||
        !check(fixture.services, script,
               "return globalThis.fsm.actors[0].stateMachineState==='Idle';",
               "exit-time transition is not blocked by native completion-paused state") ||
        !expect(script.tick(.25) && fixture.animation.tick(.25) && script.tick(0),
                "finish return transition and consume pending trigger") ||
        !check(fixture.services, script, R"js(
const f=globalThis.fsm,a=f.actors[0];
return a.stateMachineState==='Fire' && a.ClearStateMachine() && a.ClearStateMachine() &&
    a.stateMachineState===null && a.SetStateMachine(f.definition,f.aliases) &&
    a.SetParameters({id:1}) && a.Trigger('fail');
)js",
               "pending trigger survives unrelated transitions and explicit rebind resets machine "
               "state") ||
        !expect(script.tick(0) && fixture.animation.tick(0),
                "reject missing destination resource") ||
        !check(fixture.services, script, R"js(
const f=globalThis.fsm,a=f.actors[0];
return a.stateMachineState==='Idle' && f.failed[1]===true &&
    a.ResetTrigger('fail') && a.SetStateMachine(f.definition,f.aliases);
)js",
               "failed native action preserves source state and pending trigger"))
    {
        return false;
    }
    const auto old_run = script.status().run_id;
    fixture.world.destroy_scene();
    auto* foreign = fixture.world.create_scene("Foreign scene owner");
    if (!expect(foreign && script.tick(0) && script.status().running,
                "destroyed actor bindings prune without acquiring a replacement scene"))
    {
        return false;
    }
    std::string result;
    std::string error;
    script.stop_gameplay();
    return expect(fixture.world.scene() == foreign &&
                      !test_support::execute_script(
                          fixture.services, script,
                          {old_run, "scripts/stale-fsm.js", "exports.default=()=>true;", "null"},
                          result, error),
                  "Run cleanup rejects old callbacks and preserves foreign scene ownership");
}

bool invalid_shared_behavior(Fixture& fixture, std::string_view body, unsigned sequence)
{
    std::string source = R"js(
import {Gameplay, Scene, Animator, AnimationStateMachine, AnimationState,
        AnimationCondition} from 'engine';
)js";
    source += body;
    engine::plugins::script::ScriptSystem script(&fixture.services.loader, &fixture.world);
    const bool started = load_script(fixture, script, source, sequence);
    if (started)
    {
        if (!script.tick(.01))
        {
            return false;
        }
    }
    return expect(!script.status().running && !script.status().last_error.empty() &&
                      fixture.world.scene() == nullptr,
                  "asynchronous or shared-mutable behavior fails the Run and clears all bindings");
}
} // namespace

int main()
{
    Fixture fixture;
    engine::AppConfig app;
    app.application_name = "engine-animation-state-machine-script-test";
    app.assets_path = fixture.files.root / "builtin";
    app.persistent_data_path = fixture.files.root / "persistent";
    app.cache_path = fixture.files.root / "cache";
    if (!engine::App::initialize(app) || !engine::LogSystem::instance().initialize({}))
    {
        engine::App::shutdown();
        return 1;
    }
    const bool passed = fixture.initialize() && add_model(fixture.files) &&
                        fixture.files.publish(fixture.resources) && shared_instances(fixture) &&
                        invalid_shared_behavior(fixture, R"js(
class Bad extends AnimationState {
    count=0;
    OnEnter(c) { ++this.count; }
}
export default class extends Gameplay {
    OnInitialize() {
        const scene=Scene.Create('Bad shared state');
        const root=scene.InstantiateModel('models/character.fbx','Bad');
        const a=root.FindChild('Assembly/Body').AddComponent(Animator);
        const d=new AnimationStateMachine('Idle');
        d.AddState(new Bad('Idle','idle'));
        return a.SetStateMachine(d,{idle:'animations/move.animation'});
    }
}
)js",
                                                2) &&
                        invalid_shared_behavior(fixture, R"js(
export default class extends Gameplay {
    OnInitialize() {
        const scene=Scene.Create('Async condition');
        const root=scene.InstantiateModel('models/character.fbx','Bad');
        const a=root.FindChild('Assembly/Body').AddComponent(Animator);
        const d=new AnimationStateMachine('Idle');
        d.AddState(new AnimationState('Idle','idle'));
        d.AddState(new AnimationState('Other','idle'));
        d.AddTransition('Idle','Other',async c=>true);
        return a.SetStateMachine(d,{idle:'animations/move.animation'});
    }
}
)js",
                                                3);
    fixture.animation.shutdown();
    fixture.world.shutdown();
    fixture.resources.shutdown();
    engine::LogSystem::instance().shutdown();
    engine::App::shutdown();
    if (passed)
    {
        std::puts(
            "PASS TS animation state machines: shared definitions, transitions and lifecycle");
    }
    return passed ? 0 : 1;
}
