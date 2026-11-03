import { Debug, Gameplay, MeshRenderer, Node, Scene, Transform, UiComponent } from "engine";
import { EcsManyObjectsController } from "../controllers/ecs_many_objects_controller.js";
import { EcsAction, EcsManyObjectsModel } from "../models/ecs_many_objects_model.js";

interface Entry {
    node: Node;
    transform: Transform;
    renderer: MeshRenderer;
    group: number;
    x: number;
    z: number;
}

/** A functional ECS scene: independent entities, hierarchy and lifetime; no benchmark loop. */
export default class EcsManyObjects extends Gameplay {
    scene: Scene | null = null;
    private readonly model = new EcsManyObjectsModel();
    private controller: EcsManyObjectsController | null = null;
    private groups: Node[] = [];
    private entries: Entry[] = [];
    private retired: Entry[] = [];
    private check_cursor = 0;
    private generation = 0;
    private readonly batch = 128;
    private readonly check_budget = 1024;

    override OnInitialize(): boolean {
        this.scene = Scene.Load("scenes/ecs-many-objects.scene");
        const view = this.scene?.CreateNode("ECS laboratory UI")?.AddComponent(UiComponent);
        if (!this.scene || !view || !view.SetDocument("ui/ecs-many-objects.rml")) return false;
        this.controller = new EcsManyObjectsController(view, this.model);
        return this.controller.Bind() && this.populate(10000);
    }

    /** The normal UI and execute_project_script use the same project action queue. */
    Request(action: EcsAction): boolean {
        if (!this.scene?.IsValid()) return false;
        this.model.pending = action;
        return true;
    }

    /** Plain JSON status for MCP; these counters are checked against native entity handles. */
    Inspect() {
        return { ...this.model, pending: this.model.pending, sceneValid: this.scene?.IsValid() ?? false };
    }

    override Update(): void {
        if (!this.scene || !this.controller) return;
        const action = this.model.pending;
        this.model.pending = null;
        if (action && !this.command(action)) this.fail(`Cannot execute ${action}.`);
        if (this.model.phase === "building") this.build_batch();
        else if (this.model.phase === "checking") this.check_batch();
        if (!this.controller.Refresh()) this.fail("Cannot refresh the ECS test UI.");
    }

    private populate(count: number): boolean {
        this.retired = this.entries.slice(0, 8);
        for (const root of this.groups) if (root.IsValid() && !root.Destroy()) return false;
        this.entries = [];
        this.groups = [];
        const scene = this.scene;
        if (!scene) return false;
        for (const name of ["ECS group A", "ECS group B"]) {
            const node = scene.CreateNode(name);
            if (!node) return false;
            this.groups.push(node);
        }
        ++this.generation;
        this.model.generations = this.generation;
        this.model.requested = count;
        this.model.created = this.model.checked = this.model.visible = this.model.active = 0;
        this.model.group_visible = this.model.group_active = true;
        this.model.group_offset = 0;
        this.model.phase = "building";
        this.model.status = `Building ${count} independently addressable ECS objects...`;
        const span = Math.ceil(Math.sqrt(count)) * 1.6;
        const camera = scene.FindNode("ECS Camera")?.GetComponent(Transform);
        return !!camera && camera.SetPosition(0, span * 0.85, span * 1.15) && camera.SetRotation(-36, 0, 0);
    }

    private create(index: number): Entry | null {
        const node = this.scene?.CreateNode(`ECS ${this.generation}:${index}`);
        const transform = node?.GetComponent(Transform);
        if (!node || !transform) return null;
        // Keep the Transform handle before adding another component, which moves the archetype row.
        const renderer = node.AddComponent(MeshRenderer);
        const width = Math.ceil(Math.sqrt(this.model.requested));
        const x = (index % width - (width - 1) / 2) * 1.6;
        const z = (Math.floor(index / width) - (width - 1) / 2) * 1.6;
        const group = index % 2;
        if (!renderer || !transform.IsValid() || !node.SetParent(this.groups[group]) ||
            !transform.SetPosition(x, 0, z) || !transform.SetScale(0.6, 0.6, 0.6) ||
            !renderer.SetMesh("meshes/cube.mesh") || !renderer.material.SetType("materials/cube.material") ||
            !renderer.SetCastShadows(false)) {
            if (!node.Destroy()) Debug.LogError("Cannot clean up an incomplete ECS test object");
            return null;
        }
        return { node, transform, renderer, group, x, z };
    }

    private build_batch(): void {
        const end = Math.min(this.model.requested, this.entries.length + this.batch);
        while (this.entries.length < end) {
            const entry = this.create(this.entries.length);
            if (!entry) { this.fail("Entity creation or shared resource binding failed."); return; }
            this.entries.push(entry);
        }
        this.model.created = this.entries.length;
        if (this.entries.length === this.model.requested) this.begin_check();
    }

    private begin_check(): void {
        this.check_cursor = 0;
        this.model.checked = this.model.visible = this.model.active = 0;
        this.model.phase = "checking";
        this.model.status = "Checking every entity, component handle, world transform and inherited state...";
    }

    private check_batch(): void {
        const end = Math.min(this.entries.length, this.check_cursor + this.check_budget);
        while (this.check_cursor < end) {
            const entry = this.entries[this.check_cursor++];
            const position = entry.transform.GetWorldPosition();
            const visible = entry.group === 1 || this.model.group_visible;
            const active = entry.group === 1 || this.model.group_active;
            const x = entry.x + (entry.group === 0 ? this.model.group_offset : 0);
            if (!entry.node.IsValid() || !entry.transform.IsValid() || !entry.renderer.IsValid() ||
                !position || Math.abs(position.x - x) > 0.001 || Math.abs(position.z - entry.z) > 0.001 ||
                entry.node.visibleInHierarchy !== visible || entry.renderer.visibleInHierarchy !== visible ||
                entry.node.active !== active || entry.renderer.active !== active) {
                this.fail(`Entity ${this.check_cursor - 1} has incorrect identity, transform or inherited state.`);
                return;
            }
            this.model.visible += visible ? 1 : 0;
            this.model.active += active ? 1 : 0;
            ++this.model.checked;
        }
        if (this.check_cursor !== this.entries.length) return;
        for (const entry of this.retired) {
            if (entry.node.IsValid() || entry.transform.IsValid() || entry.renderer.IsValid() ||
                entry.transform.SetPosition(99, 99, 99)) {
                this.fail("A deleted entity or component handle revived after recreation.");
                return;
            }
        }
        this.model.stale_handles += this.retired.length * 3;
        this.retired = [];
        ++this.model.checks;
        this.model.phase = "ready";
        this.model.status = `PASS: ${this.model.checked} entities checked. Change hierarchy state or recreate objects.`;
    }

    private command(action: EcsAction): boolean {
        if (action === "1000" || action === "10000" || action === "50000") return this.populate(Number(action));
        if (this.model.phase !== "ready") return true;
        if (action === "visibility") {
            this.model.group_visible = !this.model.group_visible;
            if (!this.groups[0].SetVisible(this.model.group_visible)) return false;
        } else if (action === "activation") {
            this.model.group_active = !this.model.group_active;
            if (!this.groups[0].SetEnabled(this.model.group_active)) return false;
        } else if (action === "move") {
            this.model.group_offset = this.model.group_offset === 0 ? 4 : 0;
            if (!this.groups[0].GetComponent(Transform)?.SetPosition(this.model.group_offset, 0, 0)) return false;
        } else if (action === "reparent") {
            const entry = this.entries[0];
            entry.group = 1 - entry.group;
            if (!entry.node.SetParent(this.groups[entry.group])) return false;
        } else if (action === "recreate") {
            const previous = this.entries[0];
            if (!previous.node.Destroy()) return false;
            this.retired.push(previous);
            const next = this.create(0);
            if (!next) return false;
            this.entries[0] = next;
        }
        this.begin_check();
        return true;
    }

    private fail(message: string): void {
        this.model.phase = "failed";
        this.model.status = message;
        Debug.LogError(`ECS laboratory: ${message}`);
    }

    override OnShutdown(): void {
        this.controller?.Unbind();
        this.controller = null;
        this.model.pending = null;
        for (const root of this.groups) if (root.IsValid() && !root.Destroy()) Debug.LogError("Cannot release ECS laboratory hierarchy");
        this.groups = [];
        this.entries = [];
        this.retired = [];
    }
}
