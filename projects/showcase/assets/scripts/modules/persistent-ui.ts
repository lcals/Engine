import { Resources, ResourceHandle, SceneLoadRequest, Debug, Gameplay, Node, Scene, UiComponent, UiElement } from "engine";
import { PersistentUiController } from "../controllers/persistent_ui_controller.js";
import { LifetimeAction, PersistentUiModel } from "../models/persistent_ui_model.js";

/** Technical example: the caller owns UI lifetime; scene loading uses the normal engine API. */
export default class PersistentUi extends Gameplay {
    private readonly resources: ResourceHandle[] = [];
    private request: SceneLoadRequest | null = null;
    scene: Scene | null = null;
    private root: Node | null = null;
    private note: UiElement | null = null;
    private controller: PersistentUiController | null = null;
    private readonly model = new PersistentUiModel();

    override OnInitialize(): boolean {
        for (const path of ["scenes/persistent-ui.scene", "ui/persistent-ui.rml"]) {
            const resource = Resources.Load(path);
            if (!resource) { Debug.LogError(`Cannot load ${path}: ${Resources.lastError}`); return false; }
            this.resources.push(resource);
        }

        this.scene = Scene.Load("scenes/persistent-ui.scene");
        this.root = this.scene?.CreateNode("Persistent loading UI") ?? null;
        const view = this.root?.AddComponent(UiComponent);
        if (!this.root || !view || !this.root.SetPersistent(true) ||
            !view.SetDocument("ui/persistent-ui.rml")) return false;
        this.note = view.FindElement("lifetime-note");
        this.model.identity = `Original UI node ${this.root.id}; original note handle remains valid.`;
        this.controller = new PersistentUiController(view, this.model);
        return !!this.note && this.controller.Bind();
    }

    override Update(): void {
        if (!this.controller) return;
        const action = this.model.pending;
        this.model.pending = null;
        if (action) this.command(action);
        if (!this.controller) return;
        if (this.request) {
            const status = this.request.GetStatus();
            if (!status || status.state === "failed" || status.state === "cancelled") {
                this.model.status = status?.error || "Scene load failed; the original UI remains usable.";
                this.request.Cancel();
                this.request.Release();
                this.request = null;
                this.model.loading = false;
            } else if (status.state === "staged") {
                if (!this.request.Prepare()) Debug.LogError("Cannot seal scene candidate");
            } else if (status.state === "ready") {
                if (!this.request.Activate()) Debug.LogError("Cannot activate prepared scene");
            } else if (status.state === "active") {
                this.scene = this.request.GetScene();
                this.request.Release();
                this.request = null;
                this.model.loading = false;
                ++this.model.completed;
                this.model.status = "Scene active. Edit the same note or load the other scene.";
            } else this.model.status = `Scene ${status.state}; UI remains interactive.`;
        }
        if (!this.root?.IsValid() || !this.note?.isValid || !this.controller.bound ||
            !this.controller.Refresh()) {
            Debug.LogError("Persistent UI lost its original node, element or controller");
            this.OnShutdown();
        }
    }

    private command(action: LifetimeAction): void {
        if (action === "destroy") {
            this.OnShutdown();
            Debug.Log("Persistent UI explicitly destroyed; Back to scenes remains available.");
            return;
        }
        this.request?.Cancel();
        this.request?.Release();
        this.request = null;
        this.model.loading = false;
        if (action === "unload") {
            const unloaded = !this.scene?.IsValid() || this.scene.Destroy();
            if (unloaded) this.scene = null;
            this.model.status = unloaded ? "No gameplay scene. This original UI still accepts input." :
                "Could not unload the current scene.";
        } else if (action === "fail") {
            const started = Scene.BeginLoad("scenes/missing-persistent-ui.scene");
            started?.Cancel();
            started?.Release();
            this.model.status = started ? "Unexpected test resource exists." :
                "Missing resource rejected. UI, note and subscriptions are unchanged.";
        } else if (action === "cancel") {
            const started = Scene.BeginLoad("scenes/main.scene");
            started?.Cancel();
            started?.Release();
            this.model.status = started ? "Pending load canceled. The original UI remains usable." :
                "Could not begin the cancellation check.";
        } else {
            const path = action === "load-a" ? "scenes/main.scene" : "scenes/font-gallery.scene";
            this.request = Scene.BeginLoad(path);
            this.model.loading = this.request !== null;
            this.model.status = this.model.loading ? "Preparing scene resources..." : "Could not start loading.";
        }
    }

    override OnShutdown(): void {
        this.request?.Cancel();
        this.request?.Release();
        this.request = null;
        for (const resource of this.resources.splice(0)) {
            if (!Resources.Release(resource)) Debug.LogError("Cannot release showcase resource");
        }

        this.model.loading = false;
        this.model.pending = null;
        this.controller?.Unbind();
        this.controller = null;
        if (this.root?.IsValid() && !this.root.Destroy()) Debug.LogError("Cannot release the test UI");
        this.root = null;
        this.note = null;
    }
}
