import { Debug, Gameplay, Scene } from "engine";

export default class Game extends Gameplay {
    scene: Scene | null = null;

    override OnInitialize(): boolean {
        this.scene = Scene.Load("project/scenes/font-gallery.scene");
        if (!this.scene) return false;
        Debug.Log("3D font gallery ready.");
        return true;
    }

    override OnShutdown(): void {
        this.scene = null;
    }
}
