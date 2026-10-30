import { Debug, Gameplay, Scene } from "engine";

export default class Apartment extends Gameplay {
    scene: Scene | null = null;

    override OnInitialize(): boolean {
        this.scene = Scene.Load("project/scenes/apartment.scene");
        if (!this.scene) return false;
        Debug.Log("Modern apartment ready");
        return true;
    }

    override OnShutdown(): void {
        this.scene = null;
    }
}
