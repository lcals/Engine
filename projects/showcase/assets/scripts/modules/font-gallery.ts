import { Debug, Gameplay, Render, Scene } from "engine";

export default class Game extends Gameplay {
    scene: Scene | null = null;

    override OnInitialize(): boolean {
        if (
            !Render.SetAmbient(1, 1, 1, 0) ||
            !Render.SetExposure(1) ||
            !Render.SetFilmicTonemap(false) ||
            !Render.SetDirectionalShadows(false, 2048, 0.001) ||
            !Render.SetClearColor(0x101722ff)
        ) return false;
        this.scene = Scene.Load("project/scenes/font-gallery.scene");
        if (!this.scene) return false;
        Debug.Log("3D font gallery ready.");
        return true;
    }

    override OnShutdown(): void {
        this.scene = null;
    }
}
