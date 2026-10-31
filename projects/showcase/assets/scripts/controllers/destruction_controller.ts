import { UiController } from "engine";
import { DestructionModel } from "../models/destruction_model.js";

export class DestructionController extends UiController<DestructionModel> {
    protected override OnBind(): boolean {
        for (const action of this.model.actions) {
            const button = this.view.FindElement(`destruction-${action}`);
            if (!button || !this.Listen(button, "click", () => {
                if (!this.model.ready || this.model.pending !== null) return;
                this.model.pending = action;
                if (!this.Refresh()) throw new Error("Cannot refresh the destruction controls");
            })) return false;
        }
        return true;
    }

    protected override OnRefresh(): boolean {
        const status = this.view.FindElement("destruction-status");
        const statistics = this.view.FindElement("destruction-statistics");
        if (!status || !statistics) return false;
        for (const action of this.model.actions) {
            const button = this.view.FindElement(`destruction-${action}`);
            if (!button ||
                !button.SetEnabled(this.model.ready && this.model.pending === null) ||
                !button.SetClass("selected", action === this.model.camera)) return false;
        }
        return status.SetText(this.model.status) && statistics.SetText(this.model.statistics);
    }
}
