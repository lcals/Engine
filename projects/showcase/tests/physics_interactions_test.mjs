import { resourceStub } from "./resource_mock.mjs";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtemp, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const root = path.resolve(
    process.argv[2] ?? path.join(path.dirname(fileURLToPath(import.meta.url)), "../../.."),
);
const output = await mkdtemp(path.join(tmpdir(), "engine-physics-interactions-"));

// 运行真实范例/MVC；仅替代原生边界，不在测试中模拟 Jolt 碰撞或 Detour 移动。
const source = resourceStub + String.raw`
export const fixture={ids:[],scene:null,avatars:[],agents:[],motors:[],queries:[],overlaps:[],hit:null,
    actorPositions:0,clears:0,paths:[],subscriptions:new Map(),subscriptionId:0,offCalls:[],failSubscription:0,
    emit(contact) {
        for(const item of [...this.subscriptions.values()])
            if(item.phase===contact.phase && item.node.id===contact.self.nodeId) item.callback(contact);
    },
    click(id) { const button=this.scene.view.FindElement(id); if(!button?.enabled) throw new Error(id);
        for(const item of [...button.listeners]) item.callback({type:'click',target:button,currentTarget:button}); }
};
class Element {
    constructor(){this.text='';this.enabled=true;this.listeners=[];}
    SetText(text){this.text=text;return true;} SetEnabled(enabled){this.enabled=enabled;return true;}
}
export class UiComponent {
    constructor(){this.elements=new Map(fixture.ids.map(id=>[id,new Element()]));}
    FindElement(id){return this.elements.get(id)??null;}
}
export class UiController {
    constructor(view,model){this.view=view;this.model=model;}
    Bind(){return this.OnBind()&&this.Refresh();} Refresh(){return this.OnRefresh();}
    Listen(element,type,callback){element.listeners.push({owner:this,callback});return true;}
    Unbind(){for(const element of this.view.elements.values())element.listeners=element.listeners.filter(x=>x.owner!==this);}
}
class Component {
    constructor(node){this.node=node;this.enabled=true;}
    IsValid(){return this.node.IsValid();} SetEnabled(value){this.enabled=value;return this.IsValid();}
}
export class Transform extends Component {
    constructor(node){super(node);this.position={x:0,y:0,z:0};}
    GetWorldPosition(){return this.IsValid()?{...this.position}:null;}
    SetPosition(x,y,z){if(/(actor|motor)$/.test(this.node.name))++fixture.actorPositions;this.position={x,y,z};return true;}
    SetRotation(){return true;}
}
export class MeshRenderer extends Component {
    constructor(node){super(node);this.color=[];this.material={SetType:()=>true,SetV4:(name,...v)=>{this.color=v;return true;}};}
}
export class Animator extends Component {
    constructor(node){super(node);this.animations=[];this.speeds=[];this.plays=0;}
    SetAnimation(value){this.animations.push(value);return true;}
    SetSpeed(value){this.speed=value;this.speeds.push(value);return true;}
    Play(){++this.plays;return true;}
}
export class NavigationAgent extends Component {
    constructor(node){super(node);this.requests=[];this.state='idle';this.velocity={x:0,y:0,z:0};fixture.agents.push(this);}
    MoveTo(goal){this.requests.push({...goal});this.state='pending';return true;}
    Stop(){this.state='idle';this.stopped=(this.stopped??0)+1;return true;}
}
export class CharacterMotor extends Component {
    constructor(node){super(node);this.requests=[];this.velocity={x:0,y:0,z:0};fixture.motors.push(this);}
    SetVelocity(x,z){this.requests.push({x,z});return true;}
    ClearInput(){this.cleared=(this.cleared??0)+1;return true;}
    GetVelocity(){return this.velocity;}
}
export class Node {
    constructor(scene,name){this.scene=scene;this.sceneId=scene.id;this.id=BigInt(scene.nodes.length+1);this.name=name;
        this.live=true;this.children=[];this.components=new Map([[Transform,new Transform(this)]]);}
    IsValid(){return this.live&&fixture.scene===this.scene;}
    GetComponent(type){return this.components.get(type)??null;}
    AddComponent(type){const item=new type(this);this.components.set(type,item);return item;}
    SetParent(parent){this.parent=parent;parent.children.push(this);return true;}
    FindChild(name){fixture.paths.push(name);return name==='root/GENERIC MAN new'?this.body:null;}
    Destroy(){this.live=false;for(const child of this.children)child.Destroy();return true;}
}
let sceneSequence=0n;
export class Scene {
    constructor(){this.id=++sceneSequence;this.nodes=[];this.view=new UiComponent();
        for(const name of ['Interaction ground','Interaction UI','Event region','Overlap region','Query line','Hit marker']){
            const node=this.CreateNode(name);if(name==='Interaction UI')node.components.set(UiComponent,this.view);
            else node.AddComponent(MeshRenderer);
        }
        for(let i=0;i<32;++i)this.CreateNode('Sphere footprint '+i).AddComponent(MeshRenderer);
        for(const [name,x,z,motor] of [['North actor',-8,-3,false],['Middle actor',8,0,false],['South actor',-8,3,false],['North motor',-8,-6,true],['South motor',8,6,true]]){
            const node=this.CreateNode(name);node.GetComponent(Transform).position={x,y:.05,z};node.AddComponent(motor?CharacterMotor:NavigationAgent);
        }
    }
    static Load(){const scene=new Scene();fixture.scene=scene;return scene;}
    CreateNode(name){const node=new Node(this,name);this.nodes.push(node);return node;}
    FindNode(name){return this.nodes.find(n=>n.name===name&&n.live)??null;}
    InstantiatePrefab(path,name){const node=this.CreateNode(name),body=this.CreateNode('GENERIC MAN new');body.AddComponent(MeshRenderer);
        body.SetParent(node);node.body=body;fixture.avatars.push(node);return node;}
}
export const Navigation={Build(){return true;},Clear(){++fixture.clears;return true;}};
export const Physics={
    Raycast(...args){fixture.queries.push({type:'ray',args});return fixture.hit;},
    SphereCast(...args){fixture.queries.push({type:'sphere',args});return fixture.hit;},
    OverlapBox(...args){fixture.queries.push({type:'box',args});return fixture.overlaps;},
    OverlapSphere(...args){fixture.queries.push({type:'overlap-sphere',args});return fixture.overlaps;},
    OnTriggerEnter(node,callback){return subscribe('begin',node,callback);},
    OnTriggerStay(node,callback){return subscribe('stay',node,callback);},
    OnTriggerExit(node,callback){return subscribe('end',node,callback);},
    Off(id){fixture.offCalls.push(id);return fixture.subscriptions.delete(id);},
    IsObjectValid(value){return value.valid!==false&&value.sceneId===fixture.scene.id&&value.node?.IsValid();},
    IsHitValid(value){return value.hit&&this.IsObjectValid(value);},
};
function subscribe(phase,node,callback) {
    const id=++fixture.subscriptionId;
    if(fixture.failSubscription===id) return 0;
    fixture.subscriptions.set(id,{phase,node,callback}); return id;
}
export class Gameplay {}
export const Debug={Log(){},LogWarning(){}};
export const Time={deltaTime:.25};
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
    const { fixture, Transform, MeshRenderer, CharacterMotor, Animator } = await load(
        "node_modules/engine/index.js",
    );
    const { default: Game } = await load("scripts/modules/physics-interactions.js");
    const markup = await readFile(
        path.join(root, "projects/showcase/assets/ui/physics-interactions.rml"),
        "utf8",
    );
    fixture.ids = [...markup.matchAll(/\bid="([^"]+)"/g)].map((match) => match[1]);
    const game = new Game();
    const click = (action) => {
        fixture.click(`interaction-${action}`);
        game.Update();
    };
    const text = (id) => fixture.scene.view.FindElement(`interaction-${id}`).text;
    const object = (name) => {
        const node = fixture.scene.FindNode(name);
        return {
            sceneId: node.sceneId,
            nodeId: node.id,
            kind: node.GetComponent(CharacterMotor) ? "character" : "collider",
            instanceId: node.id + 100n,
            node,
        };
    };
    const hit = (value) => ({
        ...value,
        hit: true,
        point: { x: 2, y: 1, z: 0 },
        normal: { x: -1, y: 0, z: 0 },
        fraction: 0.6,
        distance: 12,
    });
    try {
        assert.equal(game.OnInitialize(), true);
        assert.equal(fixture.agents.length, 3);
        assert.equal(fixture.motors.length, 2);
        assert.equal(new Set(fixture.avatars.map((value) => value.body)).size, 5);
        assert.ok(fixture.paths.every((value) => value === "root/GENERIC MAN new"));
        assert.equal(fixture.actorPositions, 0, "actor roots are never teleported from TS");
        const animators = fixture.avatars.map((value) => value.body.GetComponent(Animator));
        const movers = [...fixture.agents, ...fixture.motors];
        const assert_continuous_animation = () => {
            for (const animator of animators) {
                assert.deepEqual(animator.animations, ["models/locomotions.fbx/animations/walk"]);
                assert.equal(animator.plays, 1, "speed changes never restart the current clip");
            }
        };
        for (const mover of movers) mover.velocity = { x: 1.068975, y: 0, z: 0 };
        game.Update();
        assert.ok(animators.every((value) => value.speed === 1), "measured walk speed plays at 1x");
        for (const mover of movers) mover.velocity = { x: 0, y: 5, z: 3 };
        game.Update();
        assert.ok(
            animators.every((value) => value.speed > 2.8 && value.speed < 2.81),
            "both mover types use horizontal speed without the former 2.5x cap",
        );
        movers[0].velocity = { x: 100, y: 0, z: 0 };
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
        assert.deepEqual(
            [...fixture.subscriptions.values()].map((value) => value.phase),
            ["begin", "stay", "end"],
            "initialize registers native Unity-style trigger subscriptions",
        );
        const middle = object("Middle actor"),
            motor = object("North motor"),
            zone = object("Event region");
        fixture.overlaps = [middle, motor, { ...object("South actor"), valid: false }, middle];
        game.Update();
        assert.match(text("overlap"), /2 of 5/);
        assert.deepEqual(fixture.queries.at(-1).args.slice(0, 2), [
            { x: 0, y: 1, z: 0 },
            { x: 3, y: 1, z: 7 },
        ]);
        const actorBody = fixture.avatars
            .find((value) => value.name === "Middle actor avatar")
            .body.GetComponent(MeshRenderer);
        assert.deepEqual(actorBody.color, [0.2, 0.9, 0.4, 1]);
        fixture.hit = hit(middle);
        click("probe");
        assert.match(text("hit"), /Middle actor.*collider.*12.00 m/);
        assert.deepEqual(actorBody.color, [1, 0.7, 0.12, 1]);
        const ray = fixture.queries.findLast((value) => value.type === "ray");
        assert.deepEqual(ray.args[0], { x: -10, y: 1, z: 0 });
        assert.equal(ray.args[2].includeCharacters, true);
        assert.equal(ray.args[2].includeSensors, true);
        assert.equal(ray.args[2].includeStatic, false);
        click("cast");
        click("target");
        click("target");
        fixture.hit = hit(motor);
        click("probe");
        assert.match(text("hit"), /North motor.*character/);
        const sphere = fixture.queries.findLast((value) => value.type === "sphere");
        assert.equal(sphere.args[2], 0.12);
        assert.deepEqual(sphere.args[0], { x: -10, y: 1, z: -6 });
        click("region");
        assert.equal(fixture.queries.at(-1).type, "overlap-sphere");
        assert.equal(fixture.queries.at(-1).args[1], 6.8);
        assert.equal(
            fixture.scene.FindNode("Overlap region").GetComponent(MeshRenderer).enabled,
            false,
        );
        assert.equal(
            fixture.scene.FindNode("Sphere footprint 0").GetComponent(MeshRenderer).enabled,
            true,
        );
        const event = (phase, self = zone, other = middle, step = 1n) => ({
            frame: 10n,
            step,
            phase,
            self,
            other,
            point: { x: 0, y: 1, z: 0 },
            normal: { x: 1, y: 0, z: 0 },
            isTrigger: true,
        });
        for (const contact of [
            event("begin"),
            event("stay", zone, motor),
            event("end", zone, { ...middle, valid: false }, 2n),
            event("begin", { ...zone, sceneId: 99n }, middle),
            { ...event("begin"), isTrigger: false },
        ])
            fixture.emit(contact);
        game.Update();
        assert.equal(text("events"), "1 enter | 1 stay | 1 exit");
        game.Update();
        assert.equal(
            text("events"),
            "1 enter | 1 stay | 1 exit",
            "Update observes callback state without polling or replaying contact snapshots",
        );
        fixture.emit(event("stay", zone, motor, 3n));
        game.Update();
        assert.equal(
            text("events"),
            "1 enter | 2 stay | 1 exit",
            "distinct native callbacks in one frame are not discarded by script frame deduplication",
        );
        click("clear");
        game.Update();
        assert.equal(
            text("events"),
            "0 enter | 0 stay | 0 exit",
            "clear does not replay old contact events",
        );
        fixture.emit({ ...event("stay"), frame: 11n });
        game.Update();
        assert.equal(text("events"), "0 enter | 1 stay | 0 exit");
        click("pause");
        assert.ok(fixture.agents.every((agent) => agent.stopped === 1));
        assert.ok(fixture.motors.every((value) => value.cleared === 1));
        // Feed the native stopped velocity; the boundary stub does not simulate deceleration.
        for (const mover of movers) mover.velocity = { x: 0, y: 0, z: 0 };
        game.Update();
        assert.ok(animators.every((value) => value.speed === 0), "stopped movers freeze the pose");
        const requests = fixture.agents.map((agent) => agent.requests.length);
        click("reverse");
        assert.deepEqual(
            fixture.agents.map((agent) => agent.requests.length),
            requests,
            "reverse while paused does not resume movement",
        );
        click("pause");
        assert.deepEqual(
            fixture.agents.map((agent) => agent.requests.length),
            requests.map((value) => value + 1),
        );
        for (const mover of movers) mover.velocity = { x: -1.068975, y: 0, z: 0 };
        game.Update();
        assert.ok(animators.every((value) => value.speed === 1), "movement resumes the walk rate");
        assert_continuous_animation();
        fixture.hit = { ...hit(middle), valid: false };
        click("probe");
        assert.match(text("hit"), /No current physical object/);
        assert.equal(
            fixture.scene.FindNode("Hit marker").GetComponent(MeshRenderer).enabled,
            false,
        );
        assert.equal(fixture.actorPositions, 0);
        game.OnShutdown();
        assert.equal(
            fixture.subscriptions.size,
            0,
            "shutdown explicitly removes all native subscriptions",
        );
        assert.equal(fixture.offCalls.length, 3);
        assert.ok(fixture.avatars.every((value) => !value.IsValid()));
        assert.equal(
            [...fixture.scene.view.elements.values()].flatMap((value) => value.listeners).length,
            0,
        );
        assert.equal(fixture.clears, 1);
        console.log(
            "PASS physical interactions: motor/crowd queries, feedback, native Enter/Stay/Exit subscriptions and cleanup",
        );
    } finally {
        game.OnShutdown();
    }
    const failing = new Game();
    fixture.failSubscription = fixture.subscriptionId + 2;
    const previousOff = fixture.offCalls.length;
    try {
        assert.equal(
            failing.OnInitialize(),
            false,
            "a failed subscription rejects partial initialization",
        );
        assert.equal(
            fixture.subscriptions.size,
            0,
            "earlier successful registrations are immediately released",
        );
        assert.equal(fixture.offCalls.length, previousOff + 1);
    } finally {
        failing.OnShutdown();
    }
} finally {
    await rm(output, { recursive: true, force: true });
}
