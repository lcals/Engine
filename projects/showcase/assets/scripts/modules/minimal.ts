import { Resources, ResourceHandle, Debug, Gameplay, Transform, Scene, Time } from "engine";

export default class Game extends Gameplay {
    private readonly resources: ResourceHandle[] = [];
    scene: Scene | null = null;
    private cube: Transform | null = null;
    private angle = 0;

    override OnInitialize(): boolean {
        for (const path of ["scenes/main.scene"]) {
            const resource = Resources.Load(path);
            if (!resource) { Debug.LogError(`Cannot load ${path}: ${Resources.lastError}`); return false; }
            this.resources.push(resource);
        }

        this.scene = Scene.Load("scenes/main.scene");
        const cube = this.scene?.FindNode("Cube");
        this.cube = cube?.GetComponent(Transform) ?? null;
        if (!this.cube) return false;
        Debug.Log("Game.OnInitialize: PBR cube scene ready");
        return true;
    }

    override Update(): void {
        this.angle = (this.angle + Time.deltaTime * 35) % 360;
        if (this.cube && !this.cube.SetRotation(18, 25 + this.angle, 0)) {
            throw new Error("Cube is no longer part of the active scene");
        }
    }

    override OnShutdown(): void {
        for (const resource of this.resources.splice(0)) {
            if (!Resources.Release(resource)) Debug.LogError("Cannot release showcase resource");
        }

        this.cube = null;
        this.scene = null;
        Debug.Log("Game.OnShutdown: scene released");
    }
}
