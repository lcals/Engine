export const lifetime_actions = ["load-a", "load-b", "cancel", "fail", "unload", "destroy"] as const;
export type LifetimeAction = typeof lifetime_actions[number];

export class PersistentUiModel {
    pending: LifetimeAction | null = null;
    loading = false;
    completed = 0;
    events = 0;
    status = "Edit the note, then switch scenes. The same UI and subscriptions stay alive.";
    identity = "";
}
