import { Camera, Debug, DestructionComponent, Gameplay, Scene, UiComponent } from "engine";
import { DestructionController } from "../controllers/destruction_controller.js";
import { DestructionAction, DestructionModel } from "../models/destruction_model.js";

export default class Game extends Gameplay {
    scene: Scene | null = null;
    private tower: DestructionComponent | null = null;
    private wall: DestructionComponent | null = null;
    private column: DestructionComponent | null = null;
    private controller: DestructionController | null = null;
    private readonly model = new DestructionModel();

    override OnInitialize(): boolean {
        this.scene = Scene.Load("scenes/destruction.scene");
        this.tower = this.scene?.FindNode("Tower")?.GetComponent(DestructionComponent) ?? null;
        this.wall = this.scene?.FindNode("Wall")?.GetComponent(DestructionComponent) ?? null;
        this.column = this.scene?.FindNode("Column")?.GetComponent(DestructionComponent) ?? null;
        const view = this.scene?.FindNode("Destruction UI")?.GetComponent(UiComponent);
        if (!this.tower || !this.wall || !this.column || !view) return false;
        this.controller = new DestructionController(view, this.model);
        return this.controller.Bind();
    }

    override Update(): void {
        const parts = [this.tower, this.wall, this.column];
        const ready = parts.every(part => part?.IsValid() && part.ready);
        let changed = this.model.ready !== ready;
        if (changed && ready) this.model.status = "Ready. Break the wall or remove the tower support to release physical groups.";
        this.model.ready = ready;
        const request = this.model.pending;
        if (request !== null) {
            this.model.pending = null;
            if (!ready || !this.perform(request)) {
                this.model.status = parts.find(part => part?.lastError)?.lastError || `Could not complete ${request}.`;
                Debug.LogWarning(this.model.status);
            }
            changed = true;
        }
        const failure = parts.find(part => part?.lastError)?.lastError;
        if (!ready && failure && this.model.status !== failure) {
            this.model.status = failure;
            changed = true;
        }
        const chunks = parts.reduce((sum, part) => sum + (part?.chunkCount ?? 0), 0);
        const groups = parts.reduce((sum, part) => sum + (part?.groupCount ?? 0), 0);
        const dynamic = parts.reduce((sum, part) => sum + (part?.dynamicGroups ?? 0), 0);
        const statistics = `${chunks} chunks / ${groups} groups / ${dynamic} dynamic groups`;
        if (statistics !== this.model.statistics) {
            this.model.statistics = statistics;
            changed = true;
        }
        if (changed && !this.controller?.Refresh()) throw new Error("Cannot refresh the destruction view");
    }

    private perform(action: DestructionAction): boolean {
        switch (action) {
            case "wall":
                if (!this.wall?.DamageSphere(-11, 3, 1.6, 3.4, 10, 7000)) return false;
                this.model.status = "Wall damaged. Neighboring intact fragments remain one physical group.";
                return true;
            case "column":
                if (!this.column?.DamageSphere(10, 1.4, 1.2, 2.5, 10, 20000)) return false;
                this.model.status = "Column support broken. Detached sections collide, fall and settle on the ground.";
                return true;
            case "foundation":
                if (!this.tower?.DamageSphere(0.7, 1, 0.5, 6, 10, 60000)) return false;
                this.model.status = "Tower base damaged. Connected levels move together; surviving contacts may still support them.";
                return true;
            case "top":
                if (!this.tower?.DamageSphere(0.7, 17, 0.5, 6, 10, 16000)) return false;
                this.model.status = "Tower upper region damaged at its original world position. Reset first if it has already fallen.";
                return true;
            case "reset":
                for (const part of [this.tower, this.wall, this.column]) if (!part?.Reset()) return false;
                this.model.status = "All three specimens returned to their original support graphs and poses.";
                return true;
            case "overview":
            case "front": {
                const name = action === "overview" ? "Overview camera" : "Front camera";
                const camera = this.scene?.FindNode(name)?.GetComponent(Camera);
                if (!camera || !this.scene?.SetActiveCamera(camera)) return false;
                this.model.camera = action;
                this.model.status = action === "overview" ? "Overview of all three specimens." : "Front view of the tower and detached groups.";
                return true;
            }
        }
    }

    override OnShutdown(): void {
        this.controller?.Unbind();
        this.controller = null;
        this.tower = null;
        this.wall = null;
        this.column = null;
        this.scene = null;
    }
}
