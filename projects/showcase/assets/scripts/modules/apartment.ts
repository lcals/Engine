import { Resources, ResourceHandle, Debug, Gameplay, Scene } from "engine";

export default class Apartment extends Gameplay {
    private readonly resources: ResourceHandle[] = [];
    scene: Scene | null = null;

    override OnInitialize(): boolean {
        for (const path of ["scenes/apartment.scene"]) {
            const resource = Resources.Load(path);
            if (!resource) { Debug.LogError(`Cannot load ${path}: ${Resources.lastError}`); return false; }
            this.resources.push(resource);
        }

        this.scene = Scene.Load("scenes/apartment.scene");
        if (!this.scene) return false;
        Debug.Log("Modern apartment ready");
        return true;
    }

    override OnShutdown(): void {
        for (const resource of this.resources.splice(0)) {
            if (!Resources.Release(resource)) Debug.LogError("Cannot release showcase resource");
        }

        this.scene = null;
    }
}
