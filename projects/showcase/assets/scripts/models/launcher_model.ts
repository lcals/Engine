export interface LauncherChoice {
    readonly button: string;
    readonly title: string;
    readonly scene: string;
}

export class LauncherModel {
    readonly choices: readonly LauncherChoice[] = [
        { button: "scene-cube", title: "PBR essentials", scene: "scenes/main.scene" },
        { button: "scene-apartment", title: "Modern apartment", scene: "scenes/apartment.scene" },
        {
            button: "scene-courtyard",
            title: "Animation courtyard",
            scene: "scenes/playground.scene",
        },
        { button: "scene-gallery", title: "Letters in space", scene: "scenes/font-gallery.scene" },
        { button: "scene-effects", title: "Effects laboratory", scene: "scenes/effects.scene" },
        { button: "scene-terrain", title: "Terrain laboratory", scene: "scenes/terrain.scene" },
        {
            button: "scene-destruction",
            title: "Destruction laboratory",
            scene: "scenes/destruction.scene",
        },
        {
            button: "scene-integration",
            title: "Combined destruction laboratory",
            scene: "scenes/destruction-integration.scene",
        },
        { button: "scene-shooting", title: "Shooting range", scene: "scenes/shooting-range.scene" },
        {
            button: "scene-navigation",
            title: "Navigation laboratory",
            scene: "scenes/navigation.scene",
        },
    ];
    selected: LauncherChoice | null = null;
    pending: LauncherChoice | "menu" | null = null;
    error: string | null = null;
    selection_count = 0;
}
