import { Resources, ResourceHandle, Debug, Gameplay, Scene } from "engine";

export default class Game extends Gameplay {
    private readonly resources: ResourceHandle[] = [];
    scene: Scene | null = null;

    override OnInitialize(): boolean {
        for (const path of ["scenes/effects.scene"]) {
            const resource = Resources.Load(path);
            if (!resource) { Debug.LogError(`Cannot load ${path}: ${Resources.lastError}`); return false; }
            this.resources.push(resource);
        }

        this.scene = Scene.Load("scenes/effects.scene");
        if (!this.scene) return false;
        Debug.Log("Effects laboratory ready: laser sprites and animated arrow models.");
        return true;
    }

    override OnShutdown(): void {
        for (const resource of this.resources.splice(0)) {
            if (!Resources.Release(resource)) Debug.LogError("Cannot release showcase resource");
        }

        this.scene = null;
    }
}
