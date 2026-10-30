import { Debug, Gameplay, Render, Scene, UiComponent } from "engine";
import { LauncherController } from "../controllers/launcher_controller.js";
import { LauncherModel } from "../models/launcher_model.js";

export default class Game extends Gameplay {
    private controller: LauncherController | null = null;

    override OnInitialize(): boolean {
        if (!Render.SetClearColor(0x0e1726ff)) return false;
        const scene = Scene.Load("project/scenes/launcher.scene");
        const view = scene?.FindNode("Launcher UI")?.GetComponent(UiComponent);
        if (!view) return false;
        this.controller = new LauncherController(view, new LauncherModel());
        if (!this.controller.Bind()) return false;
        Debug.Log("Scene launcher MVC ready: input updates the model and view.");
        return true;
    }

    override OnShutdown(): void {
        this.controller?.Unbind();
        this.controller = null;
    }
}
