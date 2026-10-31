import { UiController } from "engine";
import { DestructionIntegrationModel } from "../models/destruction_integration_model.js";

export class DestructionIntegrationController extends UiController<DestructionIntegrationModel> {
    protected override OnBind(): boolean {
        for (const action of this.model.actions) {
            const button = this.view.FindElement(`integration-${action}`);
            if (!button || !this.Listen(button, "click", () => {
                if (!this.model.ready || this.model.pending !== null) return;
                this.model.pending = action;
                if (!this.Refresh()) throw new Error("Cannot refresh the integration controls");
            })) return false;
        }
        return true;
    }

    protected override OnRefresh(): boolean {
        const status = this.view.FindElement("integration-status");
        const statistics = this.view.FindElement("integration-statistics");
        const impacts = this.view.FindElement("integration-impacts");
        if (!status || !statistics || !impacts) return false;
        for (const action of this.model.actions) {
            const button = this.view.FindElement(`integration-${action}`);
            if (!button ||
                !button.SetEnabled(this.model.ready && this.model.pending === null) ||
                !button.SetClass("selected", action === this.model.camera)) return false;
        }
        return status.SetText(this.model.status) &&
            statistics.SetText(this.model.statistics) && impacts.SetText(this.model.impacts);
    }
}
