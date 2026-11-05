import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtemp, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const root = path.resolve(
    process.argv[2] ?? path.join(path.dirname(fileURLToPath(import.meta.url)), "../../.."),
);
const output = await mkdtemp(path.join(tmpdir(), "engine-navigation-crowd-"));

// 只替代宿主边界。实际范例模块、MVC、目标配置和清理流程参与回归，不模拟 Detour。
const source = String.raw`
export const fixture = { scene: null, ids: [], agents: [], animators: [], paths: [], positions: 0,
    builds: [], replacements: [], clears: 0, reject: 0, stops: 0, events: [],
    failReplace: false, failBuild: false, failBrush: 0, brushes: 0, terrain: null,
    click(id) {
        const button = this.scene.view.FindElement(id);
        if (!button?.enabled) throw new Error('Unavailable button: ' + id);
        for (const item of [...button.listeners]) item.callback({type:'click',target:button,currentTarget:button});
    }
};
class Element {
    constructor() { this.text=''; this.enabled=true; this.classes=new Set(); this.listeners=[]; }
    SetText(value) { this.text=value; return true; }
    SetEnabled(value) { this.enabled=value; return true; }
    SetClass(name,value) { value ? this.classes.add(name) : this.classes.delete(name); return true; }
}
export class UiComponent {
    constructor() { this.elements=new Map(fixture.ids.map(id=>[id,new Element()])); }
    FindElement(id) { return this.elements.get(id) ?? null; }
}
export class UiController {
    constructor(view,model) { this.view=view; this.model=model; this.bound=false; }
    Bind() { this.bound=true; return this.OnBind() && this.Refresh(); }
    Refresh() { return this.OnRefresh(); }
    Listen(element,type,callback) { element.listeners.push({owner:this,callback}); return true; }
    Unbind() {
        if (!this.bound) return;
        this.bound=false;
        for (const element of this.view.elements.values()) element.listeners=element.listeners.filter(x=>x.owner!==this);
    }
}
export class Transform {
    constructor() { this.position={x:0,y:0,z:0}; }
    SetPosition(x,y,z) { ++fixture.positions; this.position={x,y,z}; return true; }
    SetRotation() { return true; }
}
export class Camera { constructor(node) { this.node=node; } }
export class MeshRenderer {
    constructor() { this.material={ SetType:()=>true, SetV4:(name,...value)=>{this.color=value;return true;} }; }
    IsValid() { return true; }
}
export class TerrainComponent {
    constructor(node) { this.node=node; this.ready=false; this.revision=0n; this.lastError=null; fixture.terrain=this; }
    IsValid() { return this.node.IsValid(); }
    commit() {
        ++this.revision; this.ready=true;
        const renderer=this.node.GetComponent(MeshRenderer) ?? this.node.AddComponent(MeshRenderer);
        renderer.meshRevision=this.revision;
        fixture.events.push('mesh:'+this.revision);
    }
    DigSphere(x,y,z,radius) {
        ++fixture.brushes; fixture.events.push('brush');
        if (fixture.failBrush===fixture.brushes) { this.lastError='brush rejected'; return false; }
        this.commit(); return true;
    }
    Reset() { fixture.events.push('reset'); this.commit(); return true; }
}
export class Animator {
    constructor(node) { this.node=node; this.speed=0; this.animations=[]; this.speeds=[]; this.plays=0; fixture.animators.push(this); }
    IsValid() { return this.node.IsValid(); }
    SetAnimation(value) { this.animations.push(value); return true; }
    SetSpeed(value) { this.speed=value; this.speeds.push(value); return this.IsValid(); }
    Play() { ++this.plays; return true; }
}
export class NavigationAgent {
    constructor(node) { this.node=node; this.phase='idle'; this.requests=[]; this.currentVelocity={x:0,y:0,z:0}; fixture.agents.push(this); }
    IsValid() { return this.node.IsValid(); }
    Configure(value) { this.options=value; return true; }
    MoveTo(goal) {
        this.requests.push({...goal});
        if (fixture.reject>0) { --fixture.reject; return false; }
        this.phase='pending'; return this.IsValid();
    }
    Stop() { ++fixture.stops; this.phase='idle'; this.currentVelocity={x:0,y:0,z:0}; return this.IsValid(); }
    get state() { return this.IsValid() ? this.phase : null; }
    get velocity() { return this.currentVelocity; }
}
export class Node {
    constructor(scene,name) { this.scene=scene; this.name=name; this.live=true; this.parent=null; this.children=[]; this.components=new Map([[Transform,new Transform()]]); }
    IsValid() { return this.live && fixture.scene===this.scene; }
    GetComponent(type) { return this.components.get(type) ?? null; }
    AddComponent(type) { const value=new type(this); this.components.set(type,value); return value; }
    SetParent(parent) { this.parent=parent; parent.children.push(this); return true; }
    FindChild(child) { fixture.paths.push(child); return child==='root/GENERIC MAN new' ? this.body : null; }
    Destroy() { for (const child of this.children) child.Destroy(); this.live=false; return true; }
}
export class Scene {
    constructor() {
        this.nodes=[]; this.view=new UiComponent();
        for (const name of ['Navigation ground','Sector divider','Gate west','Gate east',
            'Overview camera','Crossing camera','Doorway camera','Navigation UI']) {
            const node=this.CreateNode(name);
            if (name.endsWith('camera')) node.AddComponent(Camera);
            if (name==='Navigation UI') node.components.set(UiComponent,this.view);
            if (name==='Navigation ground') node.AddComponent(TerrainComponent);
        }
    }
    static Load() { const scene=new Scene(); fixture.scene=scene; return scene; }
    CreateNode(name) { const node=new Node(this,name); this.nodes.push(node); return node; }
    FindNode(name) { return this.nodes.find(node=>node.name===name && node.live) ?? null; }
    InstantiatePrefab(path,name) {
        const root=this.CreateNode(name), body=this.CreateNode('GENERIC MAN new');
        body.SetParent(root); body.AddComponent(MeshRenderer); root.body=body; return root;
    }
    SetActiveCamera(camera) { this.camera=camera.node.name; return true; }
}
export const Navigation={
    Build(nodes,options) {
        fixture.builds.push({names:nodes.map(node=>node.name),options});
        if (fixture.failBuild) { this.lastError='build rejected'; return false; }
        if (!fixture.terrain.ready) throw new Error('Building before terrain is ready');
        ++this.revision; this.rebuiltTiles=40; return true;
    },
    Replace(nodes,minimum,maximum) {
        const revision=fixture.terrain.revision;
        if (nodes[0].GetComponent(MeshRenderer)?.meshRevision!==revision)
            throw new Error('Replacing with stale terrain mesh');
        fixture.events.push('replace:'+revision);
        fixture.replacements.push({names:nodes.map(node=>node.name),minimum,maximum,revision});
        if (fixture.failReplace) { this.lastError='replace rejected'; return false; }
        ++this.revision; this.rebuiltTiles=4; return true;
    },
    Clear() { ++fixture.clears; return true; }, lastError:null, revision:0n, rebuiltTiles:0
};
export class Gameplay {}
export const Debug={Log(){},LogWarning(){},LogError(){}};
export const Time={deltaTime:1/60};
`;

try {
    execFileSync(
        process.execPath,
        [
            path.join(root, "projects/showcase/settings/typescript/node_modules/typescript/lib/tsc.js"),
            "--project",
            path.join(root, "projects/showcase/settings/typescript/tsconfig.json"),
            "--outDir",
            output,
            "--sourceMap",
            "false",
        ],
        { stdio: "inherit" },
    );
    const engine = path.join(output, "node_modules/engine");
    await mkdir(engine, { recursive: true });
    await writeFile(path.join(output, "package.json"), '{"type":"module"}\n');
    await writeFile(
        path.join(engine, "package.json"),
        '{"type":"module","exports":"./index.js"}\n',
    );
    await writeFile(path.join(engine, "index.js"), source);
    const load = (relative) => import(pathToFileURL(path.join(output, relative)).href);
    const { fixture, Transform, Time } = await load("node_modules/engine/index.js");
    const { default: Game } = await load("scripts/modules/navigation.js");
    const markup = await readFile(
        path.join(root, "projects/showcase/assets/ui/navigation.rml"),
        "utf8",
    );
    fixture.ids = [...markup.matchAll(/\bid="([^"]+)"/g)].map((match) => match[1]);
    const game = new Game();
    const alive = () => fixture.agents.filter((agent) => agent.IsValid());
    const click = (action) => {
        fixture.click(`navigation-${action}`);
        game.Update();
    };
    const text = (id) => fixture.scene.view.FindElement(`navigation-${id}`).text;
    try {
        assert.equal(game.OnInitialize(), true);
        assert.equal(alive().length, 0, "terrain loading does not spawn agents early");
        assert.equal(fixture.builds.length, 0);
        game.Update();
        assert.equal(fixture.builds.length, 0, "Build waits for the generated terrain mesh");
        fixture.terrain.commit();
        game.Update();
        assert.equal(alive().length, 16);
        assert.equal(fixture.builds.length, 1);
        assert.deepEqual(fixture.builds[0].names, [
            "Navigation ground",
            "Sector divider",
            "Gate west",
            "Gate east",
        ]);
        for (const agent of alive()) {
            assert.equal(agent.node.parent, null, "each agent owns an unparented root");
            assert.equal(agent.options.radius, fixture.builds[0].options.agentRadius);
            assert.equal(agent.options.height, fixture.builds[0].options.agentHeight);
            assert.equal(agent.options.faceVelocity, true);
        }
        assert.equal(new Set(fixture.animators.map((animator) => animator.node)).size, 16);
        const animators = [...fixture.animators];
        const assert_continuous_animation = () => {
            for (const animator of animators) {
                assert.deepEqual(animator.animations, ["models/locomotions.fbx/animations/walk"]);
                assert.equal(animator.plays, 1, "speed changes never restart the current clip");
            }
        };
        assert.deepEqual(
            new Set(fixture.paths),
            new Set(["root/GENERIC MAN new"]),
            "model lookup stays inside each clone",
        );
        game.Update();
        assert.match(text("statistics"), /16 pending/);
        assert.match(text("statistics"), /0 arrived/, "request acceptance is not arrival");
        const writes = fixture.positions;
        for (const agent of alive()) {
            agent.phase = "moving";
            agent.currentVelocity = { x: 1.068975, y: 0, z: 0 };
        }
        Time.deltaTime = 0.25;
        game.Update();
        assert.equal(
            fixture.positions,
            writes,
            "ordinary updates never move actor transforms from TS",
        );
        assert.ok(animators.every((value) => value.speed === 1), "measured walk speed plays at 1x");
        for (const agent of alive()) agent.currentVelocity = { x: 0, y: 5, z: 3 };
        game.Update();
        assert.ok(
            animators.every((value) => value.speed > 2.8 && value.speed < 2.81),
            "3 m/s horizontal movement is calibrated to walk and is not capped at 2.5x",
        );
        alive()[0].currentVelocity = { x: 100, y: 0, z: 0 };
        game.Update();
        assert.equal(animators[0].speed, 16, "playback stays within Animator's supported limit");
        const speedCalls = animators.map((value) => value.speeds.length);
        game.Update();
        assert.deepEqual(
            animators.map((value) => value.speeds.length),
            speedCalls,
            "unchanged velocity does not resubmit animation settings",
        );
        assert_continuous_animation();
        const pitStops = fixture.stops;
        const pitRequests = alive().map((agent) => agent.requests.length);
        click("pit");
        assert.equal(fixture.replacements.length, 1);
        assert.deepEqual(fixture.replacements[0].minimum, { x: -17, y: -4, z: -5 });
        assert.deepEqual(fixture.replacements[0].maximum, { x: -7, y: 4, z: 5 });
        assert.deepEqual(
            fixture.events.slice(-3),
            ["brush", "mesh:2", "replace:2"],
            "synchronous brush commits fresh mesh before the same-frame local Replace",
        );
        assert.equal(fixture.stops, pitStops, "ordinary edits do not cancel native goals");
        assert.deepEqual(
            alive().map((agent) => agent.requests.length),
            pitRequests,
            "native Replace requeues paths; TS does not resubmit goals",
        );
        assert.equal(fixture.positions, writes, "terrain edits do not teleport agents");
        assert.match(text("terrain-statistics"), /Terrain r2.*navigation r2.*4 tiles rebuilt/);
        click("reverse");
        for (const agent of alive())
            assert.deepEqual(agent.requests.at(-1), agent.node.GetComponent(Transform).position);
        for (const agent of alive()) {
            agent.phase = "moving";
            agent.currentVelocity = { x: -1.068975, y: 0, z: 0 };
        }
        game.Update();
        click("stop");
        assert.equal(alive().filter((agent) => agent.phase === "idle").length, 16);
        assert.ok(animators.every((value) => value.speed === 0), "stopping freezes the walk pose");
        click("resume");
        assert.equal(alive().filter((agent) => agent.phase === "pending").length, 16);
        assert.ok(animators.every((value) => value.speed === 0), "pending paths keep walk frozen");
        for (const agent of alive()) {
            agent.phase = "moving";
            agent.currentVelocity = { x: -1.068975, y: 0, z: 0 };
        }
        game.Update();
        assert.ok(animators.every((value) => value.speed === 1), "movement resumes the walk rate");
        assert_continuous_animation();
        const previous = alive();
        click("count64");
        assert.equal(alive().length, 64);
        assert.ok(
            previous.every((agent) => !agent.IsValid()),
            "changing quantity deletes the former instances",
        );
        assert.equal(fixture.builds.length, 1, "agent counts do not rebuild static navigation");
        click("doorway");
        assert.equal(alive().length, 64);
        assert.equal(
            alive().filter((agent) => agent.node.GetComponent(Transform).position.z < 0).length,
            32,
        );
        assert.equal(fixture.scene.camera, "Doorway camera");
        const doorAgents = alive();
        const doorRequests = doorAgents.map((agent) => agent.requests.length);
        const doorWrites = fixture.positions;
        click("cut");
        assert.equal(fixture.brushes, 7, "six overlapping spheres cut the entire doorway floor");
        assert.deepEqual(fixture.replacements.at(-1).minimum, { x: -4, y: -4, z: -6 });
        assert.deepEqual(fixture.replacements.at(-1).maximum, { x: 28, y: 4, z: 6 });
        assert.deepEqual(alive(), doorAgents, "terrain updates preserve native agent identities");
        assert.deepEqual(
            doorAgents.map((agent) => agent.requests.length),
            doorRequests,
        );
        assert.equal(fixture.positions, doorWrites);
        // Native path result is injected; the test never imitates Detour's geometry algorithms.
        for (const agent of doorAgents) agent.phase = "unreachable";
        game.Update();
        assert.match(text("statistics"), /64 no route/);
        click("restore");
        assert.deepEqual(
            fixture.replacements.at(-1).minimum,
            { x: -17, y: -4, z: -6 },
            "Reset replaces all edited regions, including the earlier crossing pit",
        );
        assert.deepEqual(fixture.replacements.at(-1).maximum, { x: 28, y: 4, z: 6 });
        assert.deepEqual(
            doorAgents.map((agent) => agent.requests.length),
            doorRequests,
            "restore leaves native retained destinations intact",
        );
        assert.deepEqual(fixture.events.slice(-3), ["reset", "mesh:9", "replace:9"]);
        fixture.reject = 1;
        click("resume");
        assert.match(text("statistics"), /1 rejected/);
        assert.match(text("status"), /rejected/);
        alive()[0].phase = "unreachable";
        alive()[1].phase = "invalid";
        game.Update();
        assert.match(text("statistics"), /1 no route.*1 invalid/);
        click("overview");
        assert.equal(fixture.scene.camera, "Overview camera");
        click("reset");
        assert.equal(alive().length, 64);
        assert.equal(fixture.builds.length, 1);
        assert.match(text("statistics"), /0 rejected/);
        click("stop");
        const stoppedRequests = alive().map((agent) => agent.requests.length);
        click("pit");
        click("restore");
        assert.deepEqual(
            alive().map((agent) => agent.requests.length),
            stoppedRequests,
            "editing while stopped does not resume the group",
        );
        click("resume");
        const failureAgents = alive();
        const failureRequests = failureAgents.map((agent) => agent.requests.length);
        fixture.failReplace = true;
        click("pit");
        assert.ok(
            failureAgents.every((agent) => agent.phase === "idle"),
            "failed local rebuild stops agents before they consume stale navigation",
        );
        assert.equal(fixture.scene.view.FindElement("navigation-resume").enabled, false);
        assert.match(text("status"), /Navigation rebuild failed/);
        fixture.failReplace = false;
        click("retryTerrain");
        assert.deepEqual(
            failureAgents.map((agent) => agent.requests.length),
            failureRequests.map((count) => count + 1),
            "repair restores only goals canceled by failure",
        );
        assert.equal(fixture.scene.view.FindElement("navigation-resume").enabled, true);
        fixture.failBrush = fixture.brushes + 2;
        const beforePartial = fixture.replacements.length;
        click("cut");
        assert.equal(
            fixture.replacements.length,
            beforePartial + 1,
            "partial multi-brush failure still publishes successful geometry to navigation",
        );
        assert.match(text("status"), /brush rejected.*completed brushes/);
        const partial = fixture.replacements.at(-1);
        assert.equal(partial.revision, fixture.terrain.revision);
        assert.equal(fixture.builds.length, 1, "terrain edits never rebuild the entire navmesh");
        game.OnShutdown();
        assert.equal(alive().length, 0);
        assert.equal(fixture.clears, 1);
        assert.equal(
            [...fixture.scene.view.elements.values()].flatMap((element) => element.listeners)
                .length,
            0,
        );
        console.log(
            "PASS crowd sample: native agents, terrain startup, synchronous local rebuild, retained goals, failure recovery and cleanup",
        );
    } finally {
        game.OnShutdown();
    }
    fixture.failBuild = true;
    const failed = new Game();
    try {
        assert.equal(failed.OnInitialize(), true);
        fixture.terrain.commit();
        failed.Update();
        assert.equal(alive().length, 0, "failed initial nav build leaves no partial crowd");
        const attempts = fixture.builds.length;
        failed.Update();
        assert.equal(
            fixture.builds.length,
            attempts,
            "failed startup does not rebuild every frame",
        );
        assert.equal(fixture.scene.view.FindElement("navigation-resume").enabled, false);
    } finally {
        failed.OnShutdown();
    }
    assert.equal(
        [...fixture.scene.view.elements.values()].flatMap((element) => element.listeners).length,
        0,
        "failed startup still releases UI listeners on exit",
    );
} finally {
    await rm(output, { recursive: true, force: true });
}
