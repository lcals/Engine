import { Debug, Gameplay, Render, Scene } from "engine";

export default class Apartment extends Gameplay {
    scene: Scene | null = null;

    override OnInitialize(): boolean {
        if (
            !Render.SetAmbient(0.8, 0.86, 1.0, 0.35) ||
            !Render.SetExposure(1.15) ||
            !Render.SetFilmicTonemap(true) ||
            !Render.SetDirectionalShadows(true, 2048, 0.002) ||
            !Render.SetClearColor(0x1a2429ff)
        )
            return false;
        this.scene = Scene.Load("project/scenes/apartment.scene");
        if (!this.scene) return false;
        Debug.Log("Modern apartment ready");
        return true;
    }

    override OnShutdown(): void {
        this.scene = null;
    }
}
