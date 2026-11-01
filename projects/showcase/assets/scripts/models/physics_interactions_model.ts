export type PhysicsInteractionAction =
    "probe" | "target" | "cast" | "region" | "pause" | "reverse" | "clear";

export class PhysicsInteractionsModel {
    readonly actions: readonly PhysicsInteractionAction[] = [
        "probe",
        "target",
        "cast",
        "region",
        "pause",
        "reverse",
        "clear",
    ];
    pending: PhysicsInteractionAction | null = null;
    ready = false;
    paused = false;
    target = "Middle actor";
    cast: "ray" | "sphere" = "ray";
    region: "box" | "sphere" = "box";
    status = "Preparing physical query bodies and navigation...";
    lastHit = "No probe yet";
    overlap = "No overlap sample yet";
    events = "0 enter | 0 stay | 0 exit";
    lastEvent = "No trigger event yet";
}
