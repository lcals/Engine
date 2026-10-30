import { Debug, Gameplay, Render, Scene } from "engine";

export default class Game extends Gameplay {
    override OnInitialize(): boolean {
        if (!Render.SetClearColor(0x101722ff)) return false;
        if (!Scene.Load("project/scenes/font-gallery.scene")) return false;
        Debug.Log("3D font gallery ready.");
        return true;
    }
}
