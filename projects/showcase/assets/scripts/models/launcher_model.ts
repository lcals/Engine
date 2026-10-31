export interface LauncherChoice {
    readonly button: string;
    readonly title: string;
    readonly scene: string;
}

export class LauncherModel {
    readonly choices: readonly LauncherChoice[] = [
        { button: "scene-cube", title: "PBR essentials", scene: "project/scenes/main.scene" },
        { button: "scene-apartment", title: "Modern apartment", scene: "project/scenes/apartment.scene" },
        { button: "scene-courtyard", title: "Animation courtyard", scene: "project/scenes/playground.scene" },
        { button: "scene-gallery", title: "Letters in space", scene: "project/scenes/font-gallery.scene" },
        { button: "scene-effects", title: "Effects laboratory", scene: "project/scenes/effects.scene" },
        { button: "scene-terrain", title: "Terrain laboratory", scene: "project/scenes/terrain.scene" },
        { button: "scene-destruction", title: "Destruction laboratory", scene: "project/scenes/destruction.scene" },
    ];
    selected: LauncherChoice | null = null;
    pending: LauncherChoice | "menu" | null = null;
    error: string | null = null;
    selection_count = 0;
}
