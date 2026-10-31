export type IntegrationAction =
    | "dig" | "tower" | "wall" | "fill" | "reset" | "respawn"
    | "overview" | "front" | "walk";

export class DestructionIntegrationModel {
    readonly actions: readonly IntegrationAction[] = [
        "dig", "tower", "wall", "fill", "reset", "respawn", "overview", "front", "walk",
    ];
    pending: IntegrationAction | null = null;
    ready = false;
    camera: "overview" | "front" | "walk" = "overview";
    status = "Preparing terrain collision, support graphs and effect resources...";
    statistics = "Earth / support / physics / effects / audio";
    impacts = "0 qualifying contacts / 0 secondary damage events";
}
