export type NavigationMode = "crossing" | "doorway";
export type NavigationAction =
    | NavigationMode
    | "count16"
    | "count32"
    | "count64"
    | "reverse"
    | "stop"
    | "resume"
    | "reset"
    | "pit"
    | "cut"
    | "restore"
    | "retryTerrain"
    | "overview"
    | "detail";

export class NavigationModel {
    readonly actions: readonly NavigationAction[] = [
        "crossing",
        "doorway",
        "count16",
        "count32",
        "count64",
        "reverse",
        "stop",
        "resume",
        "reset",
        "pit",
        "cut",
        "restore",
        "retryTerrain",
        "overview",
        "detail",
    ];
    pending: NavigationAction | null = null;
    mode: NavigationMode = "crossing";
    count = 16;
    camera: "overview" | "detail" = "detail";
    stopped = false;
    reversed = false;
    ready = false;
    synchronized = false;
    requestFailures = 0;
    status = "Waiting for the terrain mesh before building navigation...";
    statistics = "No agents registered";
    terrainStatistics = "Terrain and navigation not ready";
}
