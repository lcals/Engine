import { Debug, Gameplay, Scene, UiComponent } from "engine";
import { LauncherController } from "../controllers/launcher_controller.js";
import { LauncherChoice, LauncherModel } from "../models/launcher_model.js";
import { ShowcaseGame } from "../models/showcase_cases.js";

export default class Game extends Gameplay {
    private controller: LauncherController | null = null;
    private active: ShowcaseGame | null = null;
    private readonly model = new LauncherModel();

    override OnInitialize(): boolean {
        if (!this.open_menu()) return false;
        Debug.Log("Scene launcher ready: open a world and return with Back to scenes.");
        return true;
    }

    override Update(): void {
        const request = this.model.pending;
        if (request === null) {
            this.active?.Update();
            return;
        }
        this.model.pending = null;
        this.close_active();
        const ready = request === "menu" ? this.open_menu() : this.open_scene(request);
        if (!ready) throw new Error("Cannot restore the scene launcher after navigation");
    }

    private open_menu(): boolean {
        this.model.selected = null;
        this.model.pending = null;
        if (!this.controller) {
            const scene = Scene.Load("scenes/launcher.scene");
            const root = scene?.FindNode("Launcher UI");
            const view = root?.GetComponent(UiComponent);
            if (!root || !view || !root.SetPersistent(true) || !this.bind_view(view)) return false;
        }
        return this.controller?.Refresh() ?? false;
    }

    private open_scene(choice: LauncherChoice): boolean {
        const game = new choice.game();
        // 初始化过程中抛错时，入口 OnShutdown 仍能关闭这次部分初始化的玩法。
        this.active = game;
        if (game.OnInitialize() !== true || !game.scene?.IsValid()) {
            this.close_active();
            return this.recover_menu(`Cannot open ${choice.title}. Choose another scene.`);
        }
        this.model.selected = choice;
        this.model.error = null;
        if (!this.controller?.Refresh()) {
            this.close_active();
            return this.recover_menu(`Cannot create navigation for ${choice.title}.`);
        }
        this.model.selection_count += 1;
        Debug.Log(`Opened ${choice.title} (${this.model.selection_count} visits).`);
        return true;
    }

    private bind_view(view: UiComponent): boolean {
        const controller = new LauncherController(view, this.model);
        this.controller = controller;
        if (!controller.Bind()) {
            this.controller = null;
            controller.Unbind();
            return false;
        }
        return true;
    }

    private close_active(): void {
        const active = this.active;
        this.active = null;
        const scene = active?.scene;
        try {
            active?.OnShutdown();
        } finally {
            if (scene?.IsValid() && !scene.Destroy())
                Debug.LogError("Cannot unload the showcase scene");
        }
    }

    private recover_menu(message: string): boolean {
        this.model.error = message;
        Debug.LogError(message);
        return this.open_menu();
    }

    override OnShutdown(): void {
        const controller = this.controller;
        this.controller = null;
        try {
            controller?.Unbind();
        } finally {
            try {
                this.close_active();
            } finally {
                this.model.pending = null;
                this.model.selected = null;
                this.model.error = null;
            }
        }
    }
}
