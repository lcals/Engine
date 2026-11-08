import { Resources, ResourceHandle, Camera, Debug, EnvironmentComponent, Gameplay, MeshRenderer, Scene, Transform, UiComponent } from "engine";
import { RenderFeaturesController } from "../controllers/render_features_controller.js";
import { RenderFeaturesModel } from "../models/render_features_model.js";

export default class RenderFeatures extends Gameplay {
    private readonly resources: ResourceHandle[] = [];
    scene: Scene | null = null;
    private readonly model = new RenderFeaturesModel();
    private controller: RenderFeaturesController | null = null;
    private readonly meshes: MeshRenderer[] = [];
    private ground: MeshRenderer | null = null;

    override OnInitialize(): boolean {
        for (const path of ["scenes/render-features.scene", "ui/render-features.rml", "meshes/render-lod-sphere.mesh", "materials/render-features.material", "meshes/cube.mesh", "builtin://materials/default-pbr", "textures/paving-albedo.jpg", "textures/paving-normal.png", "textures/paving-orm.png"]) {
            const resource = Resources.Load(path);
            if (!resource) { Debug.LogError(`Cannot load ${path}: ${Resources.lastError}`); return false; }
            this.resources.push(resource);
        }

        this.scene = Scene.Load("scenes/render-features.scene");
        if (!this.scene) return false;
        const camera = this.scene.FindNode("Render Camera")?.GetComponent(Camera);
        if (!camera || !camera.SetFrustumCulling(true)) return false;
        const view = this.scene.CreateNode("Rendering controls")?.AddComponent(UiComponent);
        if (!view || !view.SetDocument("ui/render-features.rml")) return false;
        const environment = this.scene.FindNode("Environment")?.GetComponent(EnvironmentComponent);
        const sun = this.scene.FindNode("Sun");
        if (!environment || !sun || !environment.SetAtmosphere(true, sun)) return false;
        for (let row = 0; row < 16; ++row) for (let column = 0; column < 24; ++column) {
            const node = this.scene.CreateNode(`Shared sphere ${row}:${column}`);
            const transform = node?.GetComponent(Transform);
            const mesh = node?.AddComponent(MeshRenderer);
            if (!mesh || !transform || !transform.SetPosition((column - 11.5) * 3, 1.0, -row * 3) ||
                !mesh.SetMesh("meshes/render-lod-sphere.mesh") ||
                !mesh.material.SetType("materials/render-features.material") || !mesh.SetCastShadows(false) ||
                !mesh.SetStaticInstancing(true) || !mesh.SetStaticLod(true)) return false;
            this.meshes.push(mesh);
            ++this.model.objects;
        }
        const ground = this.scene.CreateNode("Hex surface");
        const transform = ground?.GetComponent(Transform);
        this.ground = ground?.AddComponent(MeshRenderer) ?? null;
        if (!transform || !this.ground || !transform.SetPosition(0, -0.3, -22) ||
            !transform.SetScale(40, 0.2, 28) || !this.ground.SetMesh("meshes/cube.mesh") ||
            !this.ground.material.SetType("builtin://materials/default-pbr") ||
            !this.ground.material.SetTexture("baseColorTexture", "textures/paving-albedo.jpg") ||
            !this.ground.material.SetTexture("normalTexture", "textures/paving-normal.png") ||
            !this.ground.material.SetTexture("ormTexture", "textures/paving-orm.png") ||
            !this.ground.material.SetV4("uvTransform", 12, 12, 0, 0) ||
            !this.ground.material.SetFloat("roughness", 0.7) ||
            !this.ground.SetStaticInstancing(true) || !this.ground.SetStaticLod(true)) return false;
        this.controller = new RenderFeaturesController(view, this.model);
        this.model.status = `${this.model.objects} static spheres + textured PBR ground. Runtime options are explicitly enabled on the test components.`;
        return this.controller.Bind();
    }

    Inspect() {
        return { objects: this.model.objects, atmosphere: this.model.atmosphere, hex: this.model.hex,
            distant: this.model.distant, features: this.model.features };
    }

    override Update(): void {
        if (!this.scene || !this.controller) return;
        const action = this.model.pending;
        this.model.pending = null;
        let accepted = true;
        if (action === "atmosphere") {
            const enabled = !this.model.atmosphere;
            accepted = this.scene.FindNode("Environment")?.GetComponent(EnvironmentComponent)?.SetAtmosphere(enabled, this.scene.FindNode("Sun")) ?? false;
            if (accepted) this.model.atmosphere = enabled;
        } else if (action === "hex") {
            const enabled = !this.model.hex;
            accepted = this.ground?.material.SetFloat("hexTilingEnabled", enabled ? 1 : 0) ?? false;
            if (accepted) this.model.hex = enabled;
        } else if (action === "camera") {
            const distant = !this.model.distant;
            accepted = this.scene.FindNode("Render Camera")?.GetComponent(Transform)?.SetPosition(0, distant ? 30 : 10, distant ? 90 : 25) ?? false;
            if (accepted) this.model.distant = distant;
        } else if (action === "frustum_culling") {
            const camera = this.scene.FindNode("Render Camera")?.GetComponent(Camera);
            accepted = camera?.SetFrustumCulling(!camera.frustumCullingEnabled) ?? false;
        } else if (action === "static_instancing" || action === "static_lod") {
            const enabled = action === "static_instancing"
                ? !this.meshes[0]?.staticInstancingEnabled : !this.meshes[0]?.staticLodEnabled;
            for (const mesh of this.meshes) {
                const changed = action === "static_instancing"
                    ? mesh.SetStaticInstancing(enabled) : mesh.SetStaticLod(enabled);
                accepted = changed && accepted;
            }
            if (this.ground) accepted = (action === "static_instancing"
                ? this.ground.SetStaticInstancing(enabled) : this.ground.SetStaticLod(enabled)) && accepted;
        }
        if (!accepted) this.model.status = `Cannot apply ${action}; inspect device support and feature reason.`;
        const camera = this.scene.FindNode("Render Camera")?.GetComponent(Camera);
        this.model.features.frustum_culling = camera?.frustumCullingEnabled ?? false;
        this.model.features.static_instancing = this.meshes.some(mesh => mesh.staticInstancingEnabled) ||
            (this.ground?.staticInstancingEnabled ?? false);
        this.model.features.static_lod = this.meshes.some(mesh => mesh.staticLodEnabled) ||
            (this.ground?.staticLodEnabled ?? false);
        if (!this.controller.Refresh()) Debug.LogError("Rendering laboratory UI refresh failed");
    }

    override OnShutdown(): void {
        for (const resource of this.resources.splice(0)) {
            if (!Resources.Release(resource)) Debug.LogError("Cannot release showcase resource");
        }

        this.controller?.Unbind();
        this.controller = null;
        this.meshes.length = 0;
        this.ground = null;
        this.scene = null;
    }
}
