import {
    Animator,
    CharacterMotor,
    Debug,
    Gameplay,
    MeshRenderer,
    Navigation,
    NavigationAgent,
    Node,
    Physics,
    PhysicsContact,
    Scene,
    Time,
    Transform,
    UiComponent,
    Vector3,
} from "engine";
import { walk_animation, walk_reference_speed } from "../character_animations.js";
import { PhysicsInteractionsController } from "../controllers/physics_interactions_controller.js";
import {
    PhysicsInteractionAction,
    PhysicsInteractionsModel,
} from "../models/physics_interactions_model.js";

interface Actor {
    readonly node: Node;
    readonly label: string;
    readonly lane: number;
    readonly transform: Transform;
    readonly motor: CharacterMotor | null;
    readonly agent: NavigationAgent | null;
    readonly renderer: MeshRenderer;
    readonly animator: Animator;
    goal: number;
    highlightUntil: number;
    tint: string;
    animationSpeed: number;
}

const actor_names = ["North actor", "Middle actor", "South actor", "North motor", "South motor"];
const query_options = {
    includeStatic: false,
    includeKinematic: true,
    includeDynamic: false,
    includeSensors: true,
    includeCharacters: true,
};
const region_center: Vector3 = { x: 0, y: 1, z: 0 };

/** Native movement and native queries, with only technical display/routing decisions in TS. */
export default class Game extends Gameplay {
    scene: Scene | null = null;
    private readonly model = new PhysicsInteractionsModel();
    private readonly actors: Actor[] = [];
    private readonly byId = new Map<bigint, Actor>();
    private readonly avatars: Node[] = [];
    private readonly sphereFootprint: MeshRenderer[] = [];
    private controller: PhysicsInteractionsController | null = null;
    private boxFootprint: MeshRenderer | null = null;
    private eventRegion: Node | null = null;
    private marker: Transform | null = null;
    private markerRenderer: MeshRenderer | null = null;
    private queryLine: Transform | null = null;
    private selected = 1;
    private clock = 0;
    private nextSample = 0;
    private readonly subscriptions: number[] = [];
    private contactChanged = false;
    private readonly eventCounts = { begin: 0, stay: 0, end: 0 };
    private initialized = false;

    override OnInitialize(): boolean {
        this.scene = Scene.Load("scenes/physics-interactions.scene");
        const scene = this.scene;
        const ground = scene?.FindNode("Interaction ground");
        const view = scene?.FindNode("Interaction UI")?.GetComponent(UiComponent);
        this.eventRegion = scene?.FindNode("Event region") ?? null;
        this.boxFootprint = scene?.FindNode("Overlap region")?.GetComponent(MeshRenderer) ?? null;
        this.queryLine = scene?.FindNode("Query line")?.GetComponent(Transform) ?? null;
        const marker = scene?.FindNode("Hit marker");
        this.marker = marker?.GetComponent(Transform) ?? null;
        this.markerRenderer = marker?.GetComponent(MeshRenderer) ?? null;
        if (
            !scene ||
            !ground ||
            !view ||
            !this.eventRegion ||
            !this.boxFootprint ||
            !this.queryLine ||
            !this.marker ||
            !this.markerRenderer?.SetEnabled(false)
        )
            return false;
        for (let index = 0; index < 32; ++index) {
            const renderer = scene
                .FindNode(`Sphere footprint ${index}`)
                ?.GetComponent(MeshRenderer);
            if (!renderer) return false;
            this.sphereFootprint.push(renderer);
        }
        if (
            !Navigation.Build([ground], {
                minimum: { x: -12, y: -1, z: -9 },
                maximum: { x: 12, y: 4, z: 9 },
                agentRadius: 0.3,
                agentHeight: 1.8,
                agentMaxClimb: 0.35,
                cellSize: 0.2,
                cellHeight: 0.1,
                tileSize: 32,
            })
        )
            return false;
        for (const label of actor_names) if (!this.bind_actor(label)) return false;
        if (!this.set_region()) return false;
        this.controller = new PhysicsInteractionsController(view, this.model);
        if (!this.controller.Bind()) return false;
        if (!this.bind_contacts()) return false;
        this.initialized = true;
        this.model.ready = true;
        this.model.status =
            "Five native movers ready. Queries distinguish CharacterMotor from Collider bodies.";
        return this.controller.Refresh();
    }

    private bind_actor(label: string): boolean {
        const scene = this.scene;
        const node = scene?.FindNode(label);
        const transform = node?.GetComponent(Transform);
        const position = transform?.GetWorldPosition();
        const motor = node?.GetComponent(CharacterMotor) ?? null;
        const agent = node?.GetComponent(NavigationAgent) ?? null;
        if (!scene || !node || !transform || !position || !!motor === !!agent) return false;
        const avatar = scene.InstantiateModel("models/skeleton.fbx", `${label} avatar`);
        if (!avatar) return false;
        this.avatars.push(avatar);
        if (!avatar.SetParent(node)) return false;
        const body = avatar.FindChild("root/GENERIC MAN new");
        const renderer = body?.GetComponent(MeshRenderer);
        const animator = body?.AddComponent(Animator);
        const pose = avatar.GetComponent(Transform);
        if (
            !pose?.SetPosition(0, 0.05, 0) ||
            !pose.SetRotation(0, 180, 0) ||
            !renderer?.material.SetType("materials/character.material") ||
            !animator ||
            !animator.SetAnimation(walk_animation) ||
            !animator.SetSpeed(0) ||
            !animator.Play()
        )
            return false;
        const actor: Actor = {
            node,
            label,
            lane: position.z,
            transform,
            motor,
            agent,
            renderer,
            animator,
            goal: position.x < 0 ? 8 : -8,
            highlightUntil: 0,
            tint: "",
            animationSpeed: 0,
        };
        this.actors.push(actor);
        this.byId.set(node.id, actor);
        return this.drive(actor);
    }

    private drive(actor: Actor): boolean {
        if (this.model.paused) return actor.agent ? actor.agent.Stop() : actor.motor!.ClearInput();
        if (actor.agent) return actor.agent.MoveTo({ x: actor.goal, y: 0.05, z: actor.lane });
        const position = actor.transform.GetWorldPosition();
        if (!position) return false;
        const direction = Math.sign(actor.goal - position.x);
        return (
            actor.motor!.SetVelocity(direction * 1.8, 0) &&
            actor.transform.SetRotation(0, direction > 0 ? -90 : 90, 0)
        );
    }

    private set_region(): boolean {
        if (!this.boxFootprint?.SetEnabled(this.model.region === "box")) return false;
        for (const renderer of this.sphereFootprint)
            if (!renderer.SetEnabled(this.model.region === "sphere")) return false;
        this.nextSample = 0;
        return true;
    }

    private probe(): boolean {
        const actor = this.actors[this.selected];
        const origin = { x: -10, y: 1, z: actor.lane };
        const displacement = { x: 20, y: 0, z: 0 };
        const hit =
            this.model.cast === "ray"
                ? Physics.Raycast(origin, displacement, query_options)
                : Physics.SphereCast(origin, displacement, 0.12, query_options);
        if (!hit) {
            this.model.lastHit = "Query unavailable; retry after physics has synchronized.";
            return false;
        }
        if (!hit.hit || !Physics.IsHitValid(hit)) {
            this.model.lastHit = "No current physical object hit in the selected lane.";
            return this.markerRenderer?.SetEnabled(false) === true;
        }
        const target = this.byId.get(hit.nodeId);
        if (!target) {
            this.model.lastHit = `Physical object ${hit.nodeId} hit outside the actor group.`;
            return true;
        }
        target.highlightUntil = this.clock + 1;
        this.model.lastHit = `${target.label} | ${hit.kind} #${hit.instanceId} | ${hit.distance.toFixed(2)} m`;
        this.nextSample = 0;
        return (
            !!this.marker?.SetPosition(hit.point.x, hit.point.y, hit.point.z) &&
            this.markerRenderer?.SetEnabled(true) === true
        );
    }

    private perform(action: PhysicsInteractionAction): boolean {
        if (action === "probe") return this.probe();
        if (action === "target") {
            this.selected = (this.selected + 1) % this.actors.length;
            const actor = this.actors[this.selected];
            this.model.target = actor.label;
            return this.queryLine?.SetPosition(0, 0.055, actor.lane) === true;
        }
        if (action === "cast") {
            this.model.cast = this.model.cast === "ray" ? "sphere" : "ray";
            return true;
        }
        if (action === "region") {
            this.model.region = this.model.region === "box" ? "sphere" : "box";
            return this.set_region();
        }
        if (action === "clear") {
            this.eventCounts.begin = this.eventCounts.stay = this.eventCounts.end = 0;
            this.model.events = "0 enter | 0 stay | 0 exit";
            this.model.lastEvent = "Counters cleared. Waiting for the next native callback.";
            return true;
        }
        if (action === "pause") this.model.paused = !this.model.paused;
        for (const actor of this.actors) {
            if (action === "reverse") actor.goal = -actor.goal;
            if (!this.drive(actor)) return false;
        }
        this.model.status = this.model.paused
            ? "Routes paused. Native physics and trigger observations remain active."
            : "Routes running. Motor intents and navigation goals are consumed by their native systems.";
        return true;
    }

    private bind_contacts(): boolean {
        const region = this.eventRegion;
        if (!region) return false;
        const callback = (contact: PhysicsContact) => this.on_contact(contact);
        for (const subscribe of [
            () => Physics.OnTriggerEnter(region, callback),
            () => Physics.OnTriggerStay(region, callback),
            () => Physics.OnTriggerExit(region, callback),
        ]) {
            const subscription = subscribe();
            if (subscription === 0) {
                this.release_contacts();
                return false;
            }
            this.subscriptions.push(subscription);
        }
        return true;
    }

    private release_contacts(): void {
        // Off may report false when native component lifetime handling already removed it.
        for (const subscription of this.subscriptions) Physics.Off(subscription);
        this.subscriptions.length = 0;
    }

    private on_contact(contact: PhysicsContact): void {
        if (
            !this.initialized ||
            !contact.isTrigger ||
            contact.self.nodeId !== this.eventRegion?.id ||
            contact.self.sceneId !== this.eventRegion.sceneId ||
            contact.other.sceneId !== this.eventRegion.sceneId
        )
            return;
        const actor = this.byId.get(contact.other.nodeId);
        if (!actor) return;
        // Exit describes a historical pair: the other component may already be gone.
        ++this.eventCounts[contact.phase];
        this.model.lastEvent = `${actor.label}: ${contact.phase} | frame ${contact.frame}, step ${contact.step}`;
        this.model.events = `${this.eventCounts.begin} enter | ${this.eventCounts.stay} stay | ${this.eventCounts.end} exit`;
        this.contactChanged = true;
    }

    private sample(): boolean {
        const found =
            this.model.region === "box"
                ? Physics.OverlapBox(region_center, { x: 3, y: 1, z: 7 }, query_options)
                : Physics.OverlapSphere(region_center, 6.8, query_options);
        if (!found) {
            this.model.overlap = "Overlap query unavailable; retrying next sample.";
            return true;
        }
        const ids = new Set(
            found
                .filter((object) => Physics.IsObjectValid(object) && this.byId.has(object.nodeId))
                .map((object) => object.nodeId),
        );
        this.model.overlap = `${ids.size} of ${this.actors.length} actors inside the ${this.model.region} region`;
        for (const actor of this.actors) {
            const position = actor.transform.GetWorldPosition();
            if (!position) continue;
            if (
                !this.model.paused &&
                (actor.agent?.state === "arrived" ||
                    (actor.motor && Math.abs(position.x - actor.goal) < 0.5))
            ) {
                actor.goal = -actor.goal;
                if (!this.drive(actor)) return false;
            }
            const velocity = actor.agent?.velocity ?? actor.motor?.GetVelocity();
            const speed = velocity
                ? Math.min(16, Math.hypot(velocity.x, velocity.z) / walk_reference_speed)
                : 0;
            if (Math.abs(speed - actor.animationSpeed) > 0.05) {
                if (!actor.animator.SetSpeed(speed)) return false;
                actor.animationSpeed = speed;
            }
            const tint =
                actor.highlightUntil > this.clock
                    ? "hit"
                    : ids.has(actor.node.id)
                      ? "overlap"
                      : "normal";
            if (tint !== actor.tint) {
                const color =
                    tint === "hit"
                        ? [1, 0.7, 0.12]
                        : tint === "overlap"
                          ? [0.2, 0.9, 0.4]
                          : actor.motor
                            ? [0.65, 0.35, 0.85]
                            : [0.18, 0.5, 0.9];
                if (!actor.renderer.material.SetV4("baseColor", color[0], color[1], color[2], 1))
                    return false;
                actor.tint = tint;
            }
        }
        return true;
    }

    override Update(): void {
        if (!this.initialized) return;
        this.clock += Math.max(0, Time.deltaTime);
        let refresh = this.contactChanged;
        this.contactChanged = false;
        const action = this.model.pending;
        this.model.pending = null;
        if (action !== null) {
            if (!this.perform(action))
                Debug.LogWarning("Physics interaction request could not be completed");
            refresh = true;
        }
        this.nextSample -= Time.deltaTime;
        if (this.nextSample <= 0) {
            this.nextSample = 0.25;
            if (!this.sample()) throw new Error("Cannot update physics interaction actors");
            refresh = true;
        }
        if (refresh && !this.controller?.Refresh())
            throw new Error("Cannot refresh physics interaction view");
    }

    override OnShutdown(): void {
        this.initialized = false;
        this.model.ready = false;
        this.model.pending = null;
        this.release_contacts();
        this.contactChanged = false;
        this.controller?.Unbind();
        this.controller = null;
        for (const actor of this.actors) {
            if (actor.agent?.IsValid() && !actor.agent.Stop())
                Debug.LogWarning("Cannot stop a navigation actor");
            if (actor.motor?.IsValid() && !actor.motor.ClearInput())
                Debug.LogWarning("Cannot clear a motor intent");
        }
        this.actors.length = 0;
        this.byId.clear();
        for (const avatar of this.avatars)
            if (avatar.IsValid() && !avatar.Destroy())
                Debug.LogWarning("Cannot remove an actor model");
        this.avatars.length = 0;
        this.sphereFootprint.length = 0;
        if (!Navigation.Clear()) Debug.LogWarning("Cannot clear interaction navigation");
        this.scene = null;
    }
}
