import { Debug, Gameplay, Render, Scene, UiComponent } from "engine";
import { LauncherController } from "../controllers/launcher_controller.js";
import { LauncherChoice, LauncherModel } from "../models/launcher_model.js";
import Minimal from "./minimal.js";
import Apartment from "./apartment.js";
import Courtyard from "./courtyard.js";
import FontGallery from "./font-gallery.js";

type ShowcaseGame = Minimal | Apartment | Courtyard | FontGallery;

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
        this.controller?.Unbind();
        this.controller = null;
        this.active?.OnShutdown();
        this.active = null;
        const ready = request === "menu" ? this.open_menu() : this.open_scene(request);
        if (!ready) throw new Error("Cannot restore the scene launcher after navigation");
    }

    private open_menu(): boolean {
        this.model.selected = null;
        this.model.pending = null;
        if (
            !Render.SetAmbient(1, 1, 1, 0) ||
            !Render.SetExposure(1) ||
            !Render.SetFilmicTonemap(false) ||
            !Render.SetDirectionalShadows(false, 2048, 0.001) ||
            !Render.SetClearColor(0x0e1726ff)
        ) return false;
        const scene = Scene.Load("project/scenes/launcher.scene");
        const view = scene?.FindNode("Launcher UI")?.GetComponent(UiComponent);
        if (!view) return false;
        return this.bind_view(view);
    }

    private open_scene(choice: LauncherChoice): boolean {
        let game: ShowcaseGame | null = null;
        switch (choice.scene) {
            case "project/scenes/main.scene": game = new Minimal(); break;
            case "project/scenes/apartment.scene": game = new Apartment(); break;
            case "project/scenes/playground.scene": game = new Courtyard(); break;
            case "project/scenes/font-gallery.scene": game = new FontGallery(); break;
        }
        if (!game || !game.OnInitialize() || !game.scene) {
            game?.OnShutdown();
            return this.recover_menu(`Cannot open ${choice.title}. Choose another scene.`);
        }
        this.active = game;
        this.model.selected = choice;
        this.model.error = null;
        const view = game.scene.CreateNode("Showcase navigation")?.AddComponent(UiComponent);
        if (!view || !view.SetDocument("project/ui/launcher.rml") || !this.bind_view(view)) {
            game.OnShutdown();
            this.active = null;
            return this.recover_menu(`Cannot create navigation for ${choice.title}.`);
        }
        this.model.selection_count += 1;
        Debug.Log(`Opened ${choice.title} (${this.model.selection_count} visits).`);
        return true;
    }

    private bind_view(view: UiComponent): boolean {
        const controller = new LauncherController(view, this.model);
        if (!controller.Bind()) {
            controller.Unbind();
            return false;
        }
        this.controller = controller;
        return true;
    }

    private recover_menu(message: string): boolean {
        this.model.error = message;
        Debug.LogError(message);
        return this.open_menu();
    }

    override OnShutdown(): void {
        this.controller?.Unbind();
        this.controller = null;
        this.active?.OnShutdown();
        this.active = null;
        this.model.pending = null;
        this.model.selected = null;
        this.model.error = null;
    }
}
