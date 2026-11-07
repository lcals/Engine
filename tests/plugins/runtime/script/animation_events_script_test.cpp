#include "animation_fixture.hpp"
#include "script_fixture.hpp"

#include "plugins/runtime/script/script_system.hpp"
#include "runtime/app/app.hpp"
#include "runtime/log/log_system.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

namespace
{
using namespace test_support::animation;

bool check(test_support::ResourceServices& services, engine::plugins::script::ScriptSystem& script,
           std::string_view body, const char* message)
{
    std::string source = "exports.default=()=>{";
    source += body;
    source += "};";
    std::string result, error;
    const bool passed = test_support::execute_script(
        services, script, {script.status().run_id, "scripts/event-check.js", source, "null"},
        result, error);
    if (!passed || result != "true")
    {
        std::fprintf(stderr, "animation events: %s; result=%s error=%s\n", message, result.c_str(),
                     error.c_str());
    }
    return expect(passed && result == "true", message);
}

bool exercise(Fixture& fixture)
{
    constexpr std::string_view source = R"js(
import {Gameplay, Scene, Animator} from 'engine';
const p=globalThis.eventProbe={aEvents:[],bEvents:[],clear:[],replay:[],destroy:[],disabled:[],after:0};
const request=()=>({animation:'animations/move.animation',loop:true,restart:true});
export default class extends Gameplay {
    OnInitialize() {
        p.scene=Scene.Create('Animation event integration');
        p.make=name=>{
            const root=p.scene.InstantiateModel('models/character.fbx',name);
            const body=root?.FindChild('Assembly/Body');
            const animator=body?.AddComponent(Animator);
            if(!animator?.PlayAnimation(request())) throw Error('actor playback');
            return animator;
        };
        p.a=p.make('Alpha'); p.b=p.make('Beta');
        const target={kind:'point',space:'model',point:{x:9,y:0,z:0},weight:1,enabled:true};
        if(!p.a.SetAimIkTarget('look',target) ||
           p.a.SetAimIkTarget('look',{...target,node:p.b.node}) ||
           p.a.SetAimIkTarget('look',{...target,weight:NaN}) ||
           p.a.SetAimIkTarget('look',{...target,weight:2}) ||
           p.a.GetIkTarget('look')?.point.x!==9) return false;
        const markers=[{markerId:1,time:0,name:'start'},
            {markerId:2,time:.25,name:'bool',payload:true},
            {markerId:3,time:.5,name:'number',payload:7.5},
            {markerId:4,time:.75,name:'string',payload:'step'},
            {markerId:5,time:1,name:'end',payload:null}];
        if(!p.a.SetEventMarkers(markers) ||
           !p.b.SetEventMarkers([{markerId:2,time:.5,name:'other',payload:false}]) ||
           p.a.SetEventMarkers([{markerId:6,time:.5,name:'bad',payload:{value:1}}]) ||
           p.a.SetEventMarkers([{markerId:7,time:.1,name:'duplicate'},
                               {markerId:7,time:.2,name:'duplicate'}])) return false;
        p.aToken=p.a.SubscribeEvents(e=>{
            if(!Object.isFrozen(e) || !p.a.GetBonePose('root/right') ||
               p.a.GetPlaybackInfo().playbackId!==e.playbackId) throw Error('event safe point');
            p.aEvents.push({id:e.markerId,cycle:Number(e.cycle),payload:e.payload});
        });
        p.bToken=p.b.SubscribeEvents(e=>p.bEvents.push({id:e.markerId,payload:e.payload}));
        return p.aToken!==null && p.bToken!==null;
    }
}
)js";
    constexpr std::string_view path = "scripts/events.js";
    if (!fixture.initialize() || !add_model(fixture.files) ||
        !add_bytes(fixture.files, "40000000-0000-4000-8000-000000000090", path,
                   engine::schema::AssetKind::script,
                   {reinterpret_cast<const std::byte*>(source.data()), source.size()}))
    {
        return false;
    }
    fixture.files.catalog.entries.back()->module_path = path;
    auto skeleton_data = skeleton(false);
    auto aim = std::make_unique<engine::schema::SkeletonAimIkDataT>();
    aim->name = "look";
    aim->joint_path = "root/right";
    skeleton_data.aim_ik.push_back(std::move(aim));
    std::erase_if(fixture.files.catalog.entries,
                  [](const auto& entry) { return entry->guid == target_guid; });
    flatbuffers::FlatBufferBuilder skeleton_builder;
    engine::schema::FinishSkeletonDataBuffer(
        skeleton_builder, engine::schema::SkeletonData::Pack(skeleton_builder, &skeleton_data));
    if (!add(fixture.files, target_guid, skeleton_data.path, engine::schema::AssetKind::skeleton,
             skeleton_builder))
    {
        return false;
    }
    if (!fixture.files.publish(fixture.resources))
    {
        return false;
    }
    fixture.world.destroy_scene();
    engine::plugins::script::ScriptSystem script(&fixture.services.loader, &fixture.world, nullptr,
                                                 nullptr, nullptr, nullptr, nullptr, nullptr,
                                                 &fixture.animation);
    if (!expect(script.initialize({}) &&
                    test_support::start_script(script, fixture.services,
                                               {{"content", fixture.files.root}}, path),
                "start a real Run with the AnimationSystem event sink") ||
        !expect(fixture.animation.tick(.5), "sample, commit poses and deliver the first batch") ||
        !check(fixture.services, script, R"js(
const p=globalThis.eventProbe;
return p.aEvents.length===3 && p.bEvents.length===1 &&
    p.aEvents[0].id===1 && p.aEvents[0].payload===null &&
    p.aEvents[1].payload===true && p.aEvents[2].payload===7.5 &&
    p.bEvents[0].id===2 && p.bEvents[0].payload===false;
)js",
               "callbacks execute synchronously after pose commit and isolate actor payloads") ||
        !expect(fixture.animation.tick(2), "advance across two complete animation loops") ||
        !check(
            fixture.services, script, R"js(
const p=globalThis.eventProbe;
return p.aEvents.map(e=>e.id).join(',')==='1,2,3,4,5,1,2,3,4,5,1,2,3' &&
    p.aEvents.map(e=>e.cycle).join(',')==='0,0,0,0,0,1,1,1,1,1,2,2,2' &&
    p.aEvents[3].payload==='step' && p.bEvents.length===3 &&
    !p.a.UnsubscribeEvents(p.bToken) && p.a.UnsubscribeEvents(p.aToken) &&
    !p.a.UnsubscribeEvents(p.aToken);
)js",
            "loop crossings retain stable identity/order and subscriptions belong to one actor") ||
        !expect(fixture.animation.tick(1), "advance after removing Alpha's subscription") ||
        !check(fixture.services, script, R"js(
const p=globalThis.eventProbe;
if(p.aEvents.length!==13 || p.bEvents.length!==4) return false;
if(!p.a.ClearEventMarkers() || !p.a.PlayAnimation({animation:'animations/move.animation',loop:false,restart:true}) ||
   !p.a.SetEventMarkers([{markerId:20,time:.25,name:'clear'},{markerId:21,time:.5,name:'stale'}])) return false;
p.clearToken=p.a.SubscribeEvents(e=>{p.clear.push(e.markerId);p.a.ClearEventMarkers();});
return p.clearToken!==null;
)js",
               "unsubscribing never affects another actor; prepare mutation during dispatch") ||
        !expect(fixture.animation.tick(1), "clear an event track from its first callback") ||
        !check(fixture.services, script, R"js(
const p=globalThis.eventProbe;
if(p.clear.join(',')!=='20' || !p.a.GetPlaybackInfo().completed ||
   !p.a.UnsubscribeEvents(p.clearToken) ||
   !p.a.PlayAnimation({animation:'animations/move.animation',loop:false,restart:true}) ||
   !p.a.SetEventMarkers([{markerId:30,time:.25,name:'replay'},{markerId:31,time:.5,name:'stale'}])) return false;
p.replayToken=p.a.SubscribeEvents(e=>{
    p.replay.push(e.markerId);
    if(!p.a.PlayAnimation({animation:'animations/move.animation',loop:false,restart:true})) throw Error('replay');
});
return p.replayToken!==null;
)js",
               "track revision invalidates the remainder of an already queued event batch") ||
        !expect(fixture.animation.tick(1), "restart playback within its event callback") ||
        !check(fixture.services, script, R"js(
const p=globalThis.eventProbe;
if(p.replay.join(',')!=='30' || p.a.GetPlaybackInfo().time!==0 ||
   p.a.GetPlaybackInfo().completed || !p.a.UnsubscribeEvents(p.replayToken)) return false;
p.c=p.make('Destroy');
if(!p.c.SetEventMarkers([{markerId:40,time:.1,name:'destroy'},{markerId:41,time:.2,name:'stale'}])) return false;
p.c.SubscribeEvents(e=>{p.destroy.push(e.markerId);if(!p.c.node.Destroy()) throw Error('destroy');});
return true;
)js",
               "playback identity invalidates old events even when replaying the same clip") ||
        !expect(fixture.animation.tick(.5), "destroy a component's node from its callback") ||
        !check(fixture.services, script, R"js(
const p=globalThis.eventProbe;
if(p.destroy.join(',')!=='40' || p.c.IsValid() || !p.b.IsValid() ||
   !p.b.UnsubscribeEvents(p.bToken)) return false;
p.d=p.make('Disable');
if(!p.d.SetEventMarkers([{markerId:45,time:.1,name:'disable'},
                        {markerId:46,time:.2,name:'stale'}])) return false;
p.d.SubscribeEvents(e=>{p.disabled.push(e.markerId);if(!p.d.SetEnabled(false)) throw Error('disable');});
p.d.SubscribeEvents(()=>{++p.after;});
return true;
)js",
               "destroyed identities cannot deliver later events; prepare deactivation during "
               "dispatch") ||
        !expect(fixture.animation.tick(.5), "disable an animator from the first event listener") ||
        !check(fixture.services, script, R"js(
const p=globalThis.eventProbe;
return p.disabled.join(',')==='45' && p.after===0 && !p.d.active &&
    p.a.ClearEventMarkers() &&
    p.a.PlayAnimation({animation:'animations/move.animation',loop:false,restart:true}) &&
    p.a.SetEventMarkers([{markerId:50,time:.1,name:'async-rejected'}]) &&
    p.a.SubscribeEvents(()=>Promise.resolve())!==null;
)js",
               "deactivation stops remaining listeners and queued events; prepare an invalid "
               "asynchronous "
               "callback"))
    {
        std::fprintf(stderr, "animation event Run error: %s\n", script.status().last_error.c_str());
        return false;
    }
    if (!expect(!fixture.animation.tick(.25) && !script.status().running &&
                    !script.status().last_error.empty(),
                "promise-returning event callbacks fail the current Run synchronously"))
    {
        return false;
    }
    script.stop_gameplay();
    script.shutdown();
    return expect(
        fixture.animation.tick(0) && !fixture.world.scene(),
        "stopping the Run releases event subscriptions, pending batches and scene handles");
}
} // namespace

int main()
{
    Fixture fixture;
    engine::AppConfig app;
    app.application_name = "engine-animation-events-script-test";
    app.assets_path = fixture.files.root / "builtin";
    app.persistent_data_path = fixture.files.root / "persistent";
    app.cache_path = fixture.files.root / "cache";
    if (!engine::App::initialize(app) || !engine::LogSystem::instance().initialize({}))
    {
        engine::App::shutdown();
        return 1;
    }
    const bool passed = exercise(fixture);
    fixture.animation.shutdown();
    fixture.world.shutdown();
    fixture.resources.shutdown();
    engine::LogSystem::instance().shutdown();
    engine::App::shutdown();
    if (passed)
    {
        std::puts("PASS animation event script integration and dispatch lifecycle");
    }
    return passed ? 0 : 1;
}
