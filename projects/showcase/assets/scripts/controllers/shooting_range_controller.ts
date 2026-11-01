import { UiController } from "engine";
import { ShootingRangeModel } from "../models/shooting_range_model.js";

export class ShootingRangeController extends UiController<ShootingRangeModel> {
    protected override OnBind(): boolean {
        for (const action of this.model.actions) {
            const button = this.view.FindElement(`range-${action}`);
            if (
                !button ||
                !this.Listen(button, "click", () => {
                    if (!this.model.ready || this.model.pending !== null) return;
                    this.model.pending = action;
                })
            )
                return false;
        }
        return true;
    }

    protected override OnRefresh(): boolean {
        const score = this.view.FindElement("range-score");
        const status = this.view.FindElement("range-status");
        const crosshair = this.view.FindElement("range-crosshair");
        if (!score || !status || !crosshair) return false;
        for (const action of this.model.actions) {
            const button = this.view.FindElement(`range-${action}`);
            if (
                !button ||
                !button.SetEnabled(
                    this.model.ready && (action !== "fire" || !this.model.complete),
                ) ||
                !button.SetClass("selected", action === this.model.mode)
            )
                return false;
        }
        return (
            score.SetText(
                `${this.model.hits} / ${this.model.total} targets · ${this.model.shots} shots`,
            ) &&
            status.SetText(this.model.status) &&
            crosshair.SetClass("complete", this.model.complete)
        );
    }
}
