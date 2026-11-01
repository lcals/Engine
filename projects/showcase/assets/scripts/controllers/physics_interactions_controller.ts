import { UiController } from "engine";
import { PhysicsInteractionsModel } from "../models/physics_interactions_model.js";

export class PhysicsInteractionsController extends UiController<PhysicsInteractionsModel> {
    protected override OnBind(): boolean {
        for (const action of this.model.actions) {
            const button = this.view.FindElement(`interaction-${action}`);
            if (
                !button ||
                !this.Listen(button, "click", () => {
                    if (!this.model.ready || this.model.pending !== null) return;
                    this.model.pending = action;
                    if (!this.Refresh()) throw new Error("Cannot update interaction controls");
                })
            )
                return false;
        }
        return true;
    }

    protected override OnRefresh(): boolean {
        for (const action of this.model.actions) {
            const button = this.view.FindElement(`interaction-${action}`);
            if (!button || !button.SetEnabled(this.model.ready && this.model.pending === null))
                return false;
            if (
                action === "pause" &&
                !button.SetText(this.model.paused ? "Resume routes" : "Pause routes")
            )
                return false;
            if (action === "target" && !button.SetText(`Target: ${this.model.target}`))
                return false;
            if (action === "cast" && !button.SetText(`Hit query: ${this.model.cast}`)) return false;
            if (action === "region" && !button.SetText(`Overlap: ${this.model.region}`))
                return false;
        }
        for (const [id, value] of [
            ["status", this.model.status],
            ["hit", this.model.lastHit],
            ["overlap", this.model.overlap],
            ["events", this.model.events],
            ["last-event", this.model.lastEvent],
        ]) {
            if (!this.view.FindElement(`interaction-${id}`)?.SetText(value)) return false;
        }
        return true;
    }
}
