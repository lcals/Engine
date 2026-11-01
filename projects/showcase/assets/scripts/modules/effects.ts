import { Debug, Gameplay, Scene } from "engine";

export default class Game extends Gameplay {
    scene: Scene | null = null;

    override OnInitialize(): boolean {
        this.scene = Scene.Load("scenes/effects.scene");
        if (!this.scene) return false;
        Debug.Log("Effects laboratory ready: laser sprites and animated arrow models.");
        return true;
    }

    override OnShutdown(): void {
        this.scene = null;
    }
}
