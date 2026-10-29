import { Debug, Gameplay, Transform, Scene, Time } from "engine";

export default class Game extends Gameplay {
    private cube: Transform | null = null;
    private angle = 0;

    override OnInitialize(): boolean {
        const scene = Scene.Load("project/scenes/main.scene");
        const cube = scene?.FindNode("Cube");
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
        this.cube = null;
        Debug.Log("Game.OnShutdown: scene released");
    }
}
