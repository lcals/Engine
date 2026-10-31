export type IntegrationAction =
    | "dig" | "tower" | "wall" | "fill" | "reset" | "respawn"
    | "overview" | "front" | "walk"
    | "fire" | "ground" | "rock" | "barrage" | "stop" | "rate" | "stress" | "route";

export class DestructionIntegrationModel {
    readonly actions: readonly IntegrationAction[] = [
        "dig", "tower", "wall", "fill", "reset", "respawn", "overview", "front", "walk",
        "fire", "ground", "rock", "barrage", "stop", "rate", "stress", "route",
    ];
    pending: IntegrationAction | null = null;
    ready = false;
    camera: "overview" | "front" | "walk" = "overview";
    status = "Preparing terrain collision, support graphs and effect resources...";
    statistics = "Earth / support / physics / effects / audio";
    combat = "Projectile pool ready";
    navigation = "Preparing ground route";
    impacts = "0 qualifying contacts / 0 secondary damage events";
}
