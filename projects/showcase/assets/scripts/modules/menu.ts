import { Debug, Gameplay, Render, Scene } from "engine";

export default class Game extends Gameplay {
    override OnInitialize(): boolean {
        if (!Render.SetClearColor(0x0e1726ff)) return false;
        if (!Scene.Load("project/scenes/launcher.scene")) return false;
        Debug.Log("Scene launcher ready. UI navigation bindings are not connected yet.");
        return true;
    }
}
