export type TerrainAction =
    | "dig"
    | "blast"
    | "fill"
    | "save"
    | "load"
    | "reset"
    | "overview"
    | "cave"
    | "walk"
    | "respawn";

export class TerrainModel {
    readonly actions: readonly TerrainAction[] = [
        "dig", "blast", "fill", "save", "load", "reset",
        "overview", "cave", "walk", "respawn",
    ];
    pending: TerrainAction | null = null;
    ready = false;
    camera: "overview" | "cave" | "walk" = "overview";
    status = "Preparing the terrain mesh and collision...";
    statistics = "64 x 64 m / 32 m volume height / 16-cell chunks";
}
