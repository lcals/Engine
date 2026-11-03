export const ecs_actions = ["1000", "10000", "50000", "visibility", "activation", "move", "reparent", "recreate", "verify"] as const;
export type EcsAction = typeof ecs_actions[number];
export type EcsPhase = "building" | "checking" | "ready" | "failed";

export class EcsManyObjectsModel {
    pending: EcsAction | null = null;
    requested = 10000;
    created = 0;
    checked = 0;
    visible = 0;
    active = 0;
    generations = 0;
    checks = 0;
    stale_handles = 0;
    group_visible = true;
    group_active = true;
    group_offset = 0;
    phase: EcsPhase = "building";
    status = "Creating real scene entities in batches...";
}
