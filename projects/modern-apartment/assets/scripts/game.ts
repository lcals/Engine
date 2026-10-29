import { Debug, Gameplay, Scene } from "engine";

export default class Apartment extends Gameplay {
    override OnInitialize(): boolean {
        const scene = Scene.Load("project/scenes/apartment.scene");
        if (!scene) return false;
        Debug.Log("Modern apartment ready");
        return true;
    }
}
