export type DestructionAction =
    | "wall" | "column" | "foundation" | "top" | "reset" | "overview" | "front";

export class DestructionModel {
    readonly actions: readonly DestructionAction[] = [
        "wall", "column", "foundation", "top", "reset", "overview", "front",
    ];
    pending: DestructionAction | null = null;
    ready = false;
    camera: "overview" | "front" = "overview";
    status = "Preparing fracture assets, support graphs and physical collision...";
    statistics = "3 specimens / official Blast Authoring / Jolt compound bodies";
}
