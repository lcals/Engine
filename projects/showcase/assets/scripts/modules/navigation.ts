import { Resources, ResourceHandle,
    Animator,
    Camera,
    Debug,
    Gameplay,
    MeshRenderer,
    Navigation,
    NavigationAgent,
    Node,
    Scene,
    TerrainComponent,
    Time,
    Transform,
    UiComponent,
    Vector3,
} from "engine";
import { walk_animation, walk_reference_speed } from "../character_animations.js";
import { NavigationController } from "../controllers/navigation_controller.js";
import { NavigationAction, NavigationModel } from "../models/navigation_model.js";

interface AgentView {
    readonly root: Node;
    readonly agent: NavigationAgent;
    readonly animator: Animator;
    readonly start: Vector3;
    readonly destination: Vector3;
    animationSpeed: number;
}

interface DirtyBounds {
    minimum: Vector3;
    maximum: Vector3;
}

function merge_bounds(previous: DirtyBounds | null, next: DirtyBounds): DirtyBounds {
    if (!previous) return next;
    return {
        minimum: {
            x: Math.min(previous.minimum.x, next.minimum.x),
            y: Math.min(previous.minimum.y, next.minimum.y),
            z: Math.min(previous.minimum.z, next.minimum.z),
        },
        maximum: {
            x: Math.max(previous.maximum.x, next.maximum.x),
            y: Math.max(previous.maximum.y, next.maximum.y),
            z: Math.max(previous.maximum.z, next.maximum.z),
        },
    };
}

const geometry_names = ["Navigation ground", "Sector divider", "Gate west", "Gate east"];
// Match terrain/navigation.terrain. Brush sampling extends two voxels beyond the
// sphere; neighboring cells/gradients require up to two more voxels of dirty margin.
const terrain_voxel_size = 0.5;
const colors = [
    [0.12, 0.5, 0.85],
    [0.95, 0.38, 0.1],
    [0.2, 0.73, 0.4],
    [0.68, 0.35, 0.88],
];

/** Technical crowd example. Native agents own following, avoidance and root transforms. */
export default class Game extends Gameplay {
    private readonly resources: ResourceHandle[] = [];
    scene: Scene | null = null;
    private readonly model = new NavigationModel();
    private readonly actors: AgentView[] = [];
    private readonly roots: Node[] = [];
    private readonly geometry: Node[] = [];
    private controller: NavigationController | null = null;
    private terrain: TerrainComponent | null = null;
    private started = false;
    private startupFailed = false;
    private modifiedBounds: DirtyBounds | null = null;
    private pendingBounds: DirtyBounds | null = null;
    private resumeAfterRepair = false;
    private nextSample = 0;

    override OnInitialize(): boolean {
        for (const path of ["scenes/navigation.scene", "prefabs/characters/player.prefab", "materials/character.material", "models/locomotions.fbx/animations/walk"]) {
            const resource = Resources.Load(path);
            if (!resource) { Debug.LogError(`Cannot load ${path}: ${Resources.lastError}`); return false; }
            this.resources.push(resource);
        }

        this.scene = Scene.Load("scenes/navigation.scene");
        const scene = this.scene;
        const view = scene?.FindNode("Navigation UI")?.GetComponent(UiComponent);
        if (!scene || !view) return false;
        for (const name of geometry_names) {
            const node = scene.FindNode(name);
            if (!node) return false;
            this.geometry.push(node);
        }
        this.terrain = this.geometry[0].GetComponent(TerrainComponent);
        if (!this.terrain) return false;
        this.controller = new NavigationController(view, this.model);
        return this.controller.Bind();
    }

    private start(): boolean {
        // Loading Terrain only schedules its first commit. Build once ready reports the
        // generated mesh exists; subsequent brush/reset commits are synchronous.
        if (
            !Navigation.Build(this.geometry, {
                minimum: { x: -24, y: -4, z: -16 },
                maximum: { x: 24, y: 4, z: 16 },
                cellSize: 0.2,
                cellHeight: 0.1,
                tileSize: 32,
                agentRadius: 0.3,
                agentHeight: 1.8,
                agentMaxClimb: 0.35,
            })
        ) {
            Debug.LogError(Navigation.lastError ?? "Cannot build the navigation laboratory");
            return false;
        }
        this.model.ready = true;
        this.model.synchronized = true;
        if (!this.recreate()) {
            this.model.ready = false;
            this.model.synchronized = false;
            if (!Navigation.Clear())
                Debug.LogWarning("Cannot clear the incomplete navigation mesh");
            return false;
        }
        this.record_navigation();
        Debug.Log(
            "Navigation laboratory ready: native crowd following on editable terrain, with 16/32/64 animated agents.",
        );
        return true;
    }

    private record_navigation(): void {
        this.model.terrainStatistics = `Terrain r${this.terrain?.revision ?? "?"} | navigation r${Navigation.revision ?? "?"} | ${Navigation.rebuiltTiles ?? 0} tiles rebuilt`;
    }

    private replace_ground(): boolean {
        const bounds = this.pendingBounds;
        if (!bounds) return true;
        if (!Navigation.Replace(this.geometry, bounds.minimum, bounds.maximum)) {
            if (this.model.synchronized) this.resumeAfterRepair = !this.model.stopped;
            this.stop();
            this.model.synchronized = false;
            this.model.status = `Navigation rebuild failed: ${Navigation.lastError ?? "unknown error"}. Agents stopped; retry navigation or restore terrain.`;
            this.record_navigation();
            return false;
        }
        this.pendingBounds = null;
        const repairing = !this.model.synchronized;
        this.model.synchronized = true;
        // Successful ordinary Replace preserves native destinations and requeues its own
        // paths. Only an explicit failure stop needs the retained project goals resubmitted.
        if (repairing && this.resumeAfterRepair) this.submit_goals();
        this.resumeAfterRepair = false;
        this.record_navigation();
        this.nextSample = 0;
        return true;
    }

    private edit_ground(action: "pit" | "cut" | "restore" | "retryTerrain"): boolean {
        const terrain = this.terrain;
        if (!terrain?.ready || !terrain.IsValid()) return false;
        if (action === "retryTerrain") {
            if (!this.replace_ground()) return false;
            this.model.status =
                "Current terrain committed to navigation. Retained destinations are unchanged.";
            return true;
        }
        let success = true;
        let mutationError = "";
        if (action === "restore") {
            if (!terrain.Reset()) {
                this.model.status = terrain.lastError || "Cannot restore terrain";
                return false;
            }
            if (this.modifiedBounds)
                this.pendingBounds = merge_bounds(this.pendingBounds, this.modifiedBounds);
            this.modifiedBounds = null;
        } else {
            // The pit remains bypassable. Overlapping spheres cut the entire right-hand
            // floor across z=0, down past its -4 m base; no artificial obstacle proxy.
            const centers = action === "pit" ? [-12] : [2, 6, 10, 14, 18, 22];
            const y = action === "pit" ? 0 : -1;
            const radius = action === "pit" ? 3 : 4;
            for (const x of centers) {
                const revision = terrain.revision;
                if (!terrain.DigSphere(x, y, 0, radius)) {
                    success = false;
                    mutationError = terrain.lastError || "Terrain brush failed";
                    break;
                }
                if (terrain.revision === revision) continue;
                const reach = radius + 4 * terrain_voxel_size;
                const dirty: DirtyBounds = {
                    minimum: { x: x - reach, y: -4, z: -reach },
                    maximum: { x: x + reach, y: 4, z: reach },
                };
                this.modifiedBounds = merge_bounds(this.modifiedBounds, dirty);
                this.pendingBounds = merge_bounds(this.pendingBounds, dirty);
            }
        }
        // DigSphere and Reset atomically commit the generated mesh before returning.
        // Submit even a partially completed multi-brush cut; never leave successful edits
        // paired with stale navigation when a later brush fails.
        if (!this.replace_ground()) return false;
        this.record_navigation();
        this.model.status = !success
            ? `${mutationError}. Navigation reflects the completed brushes; restore to undo them.`
            : action === "pit"
              ? "Crossing pit excavated. Native agents retain their goals and find a route around it."
              : action === "cut"
                ? "Doorway floor cut through. The two sides are disconnected; native agents report no route."
                : "Terrain restored; only the previously changed tiles were replaced. Native agents retry retained goals.";
        return success;
    }

    private formation(index: number): { start: Vector3; destination: Vector3; group: number } {
        const groupCount = this.model.mode === "crossing" ? 4 : 2;
        const groupSize = this.model.count / groupCount;
        const group = Math.floor(index / groupSize);
        const local = index % groupSize;
        const lane = ((local % 4) - 1.5) * 1.15;
        const depth = Math.floor(local / 4) * 1.1;
        if (this.model.mode === "doorway") {
            const z = (group === 0 ? -1 : 1) * (5 + depth);
            return {
                start: { x: 12 + lane, y: 0.05, z },
                destination: { x: 12 + lane, y: 0.05, z: -z },
                group,
            };
        }
        const west = { x: -20 - depth, y: 0.05, z: lane };
        const east = { x: -4 + depth, y: 0.05, z: lane };
        const north = { x: -12 + lane, y: 0.05, z: -9 - depth };
        const south = { x: -12 + lane, y: 0.05, z: 9 + depth };
        const starts = [west, east, north, south];
        const destinations = [east, west, south, north];
        return { start: starts[group], destination: destinations[group], group };
    }

    private recreate(): boolean {
        if (!this.clear_actors()) return false;
        this.model.reversed = false;
        this.model.stopped = false;
        this.model.requestFailures = 0;
        for (let index = 0; index < this.model.count; ++index) {
            if (!this.create_actor(index)) {
                if (!this.clear_actors())
                    Debug.LogWarning("Could not fully remove the incomplete crowd");
                this.model.status = "Could not create the crowd. Choose a smaller group and retry.";
                this.model.statistics = "No active agents";
                return false;
            }
        }
        if (!this.select_camera()) {
            if (!this.clear_actors()) Debug.LogWarning("Could not clear the incomplete crowd");
            return false;
        }
        this.submit_goals();
        this.nextSample = 0;
        return true;
    }

    private create_actor(index: number): boolean {
        const scene = this.scene;
        const root = scene?.CreateNode(`Navigation agent ${index + 1}`);
        if (!scene || !root) return false;
        this.roots.push(root);
        const { start, destination, group } = this.formation(index);
        const transform = root.GetComponent(Transform);
        const agent = root.AddComponent(NavigationAgent);
        if (
            !transform?.SetPosition(start.x, start.y, start.z) ||
            !agent ||
            !agent.Configure({
                radius: 0.3,
                height: 1.8,
                maxSpeed: 3,
                maxAcceleration: 8,
                separationWeight: 2,
                arrivalDistance: 0.4,
                faceVelocity: true,
            })
        )
            return false;
        const avatar = scene.InstantiatePrefab("prefabs/characters/player.prefab", `Avatar ${index + 1}`);
        if (!avatar) return false;
        if (!avatar.SetParent(root)) {
            if (!avatar.Destroy()) Debug.LogWarning("Could not remove the unattached avatar");
            return false;
        }
        // Each clone has its own path. A scene-wide name lookup would bind every agent to one body.
        const body = avatar.FindChild("root/GENERIC MAN new");
        const renderer = body?.GetComponent(MeshRenderer);
        const animator = body?.AddComponent(Animator);
        const pose = avatar.GetComponent(Transform);
        const color = colors[group];
        if (
            !pose?.SetPosition(0, 0.05, 0) ||
            !pose.SetRotation(0, 180, 0) ||
            !renderer?.material.SetType("materials/character.material") ||
            !renderer.material.SetV4("baseColor", color[0], color[1], color[2], 1) ||
            !animator ||
            !animator.SetAnimation(walk_animation) ||
            !animator.SetSpeed(0) ||
            !animator.Play()
        )
            return false;
        this.actors.push({ root, agent, animator, start, destination, animationSpeed: 0 });
        return true;
    }

    private submit_goals(): void {
        this.model.stopped = false;
        this.model.requestFailures = 0;
        for (const actor of this.actors) {
            const goal = this.model.reversed ? actor.start : actor.destination;
            if (!actor.agent.MoveTo(goal)) ++this.model.requestFailures;
        }
        this.model.status =
            this.model.requestFailures > 0
                ? `${this.model.requestFailures} target requests rejected. Resume retries the current goals.`
                : `${this.actors.length} target requests accepted. Pending agents are waiting for a route; acceptance does not mean arrival.`;
        this.nextSample = 0;
    }

    private stop(): void {
        let rejected = 0;
        for (const actor of this.actors) if (!actor.agent.Stop()) ++rejected;
        this.model.stopped = rejected === 0;
        this.model.status = rejected
            ? `${rejected} stop requests rejected. Reset to recreate the group.`
            : "Group stopped. Resume continues to the retained destinations.";
        this.nextSample = 0;
    }

    private select_camera(): boolean {
        const name =
            this.model.camera === "overview"
                ? "Overview camera"
                : this.model.mode === "crossing"
                  ? "Crossing camera"
                  : "Doorway camera";
        const camera = this.scene?.FindNode(name)?.GetComponent(Camera);
        return !!camera && this.scene?.SetActiveCamera(camera) === true;
    }

    private perform(action: NavigationAction): boolean {
        if (
            action === "pit" ||
            action === "cut" ||
            action === "restore" ||
            action === "retryTerrain"
        )
            return this.edit_ground(action);
        if (!this.model.synchronized && action !== "overview" && action !== "detail") return false;
        if (action === "crossing" || action === "doorway") {
            this.model.mode = action;
            return this.recreate();
        }
        if (action === "count16" || action === "count32" || action === "count64") {
            this.model.count = Number(action.substring(5));
            return this.recreate();
        }
        if (action === "overview" || action === "detail") {
            this.model.camera = action;
            return this.select_camera();
        }
        if (action === "reset") return this.recreate();
        if (action === "stop") this.stop();
        else {
            if (action === "reverse") this.model.reversed = !this.model.reversed;
            this.submit_goals();
        }
        return true;
    }

    override Update(): void {
        let refresh = false;
        if (
            !this.started &&
            !this.startupFailed &&
            this.terrain?.ready &&
            this.geometry[0]?.GetComponent(MeshRenderer)?.IsValid()
        ) {
            this.started = this.start();
            this.startupFailed = !this.started;
            if (!this.started)
                this.model.status =
                    Navigation.lastError ||
                    "Cannot start navigation. Return to the menu and retry.";
            refresh = true;
        }
        const action = this.model.pending;
        this.model.pending = null;
        if (action !== null) {
            if (!this.perform(action)) {
                Debug.LogWarning(this.model.status);
            }
            refresh = true;
        }
        this.nextSample -= Time.deltaTime;
        if (this.nextSample <= 0) {
            this.nextSample = 0.25;
            refresh = this.sample() || refresh;
        }
        if (refresh && !this.controller?.Refresh())
            throw new Error("Cannot refresh navigation view");
    }

    private sample(): boolean {
        const counts = { idle: 0, pending: 0, moving: 0, arrived: 0, unreachable: 0, invalid: 0 };
        for (const actor of this.actors) {
            const state = actor.agent.state ?? "invalid";
            ++counts[state];
            const velocity = actor.agent.velocity;
            const speed =
                state === "moving" && velocity
                    ? Math.min(16, Math.hypot(velocity.x, velocity.z) / walk_reference_speed)
                    : 0;
            if (actor.animator.IsValid() && Math.abs(speed - actor.animationSpeed) > 0.05) {
                if (!actor.animator.SetSpeed(speed))
                    throw new Error("Cannot update crowd animation");
                actor.animationSpeed = speed;
            }
        }
        const text = `${this.actors.length} active | ${counts.pending} pending | ${counts.moving} moving | ${counts.arrived} arrived | ${counts.idle} idle | ${counts.unreachable} no route | ${counts.invalid} invalid | ${this.model.requestFailures} rejected`;
        if (text === this.model.statistics) return false;
        this.model.statistics = text;
        return true;
    }

    private clear_actors(): boolean {
        let success = true;
        for (const actor of this.actors) {
            if (actor.agent.IsValid() && !actor.agent.Stop()) success = false;
        }
        this.actors.length = 0;
        for (const root of this.roots) if (root.IsValid() && !root.Destroy()) success = false;
        this.roots.length = 0;
        return success;
    }

    override OnShutdown(): void {
        for (const resource of this.resources.splice(0)) {
            if (!Resources.Release(resource)) Debug.LogError("Cannot release showcase resource");
        }

        this.model.ready = false;
        this.model.pending = null;
        this.controller?.Unbind();
        this.controller = null;
        this.terrain = null;
        this.geometry.length = 0;
        this.pendingBounds = null;
        this.modifiedBounds = null;
        this.started = false;
        if (!this.clear_actors()) Debug.LogWarning("Some navigation actors could not be removed");
        if (!Navigation.Clear()) Debug.LogWarning("Could not clear the navigation laboratory");
        this.scene = null;
    }
}
