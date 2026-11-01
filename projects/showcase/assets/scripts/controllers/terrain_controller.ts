import { UiController } from "engine";
import { TerrainModel } from "../models/terrain_model.js";

export class TerrainController extends UiController<TerrainModel> {
    protected override OnBind(): boolean {
        for (const action of this.model.actions) {
            const button = this.view.FindElement(`terrain-${action}`);
            if (!button || !this.Listen(button, "click", () => {
                if (!this.model.ready || this.model.pending !== null) return;
                this.model.pending = action;
                if (!this.Refresh()) throw new Error("Cannot refresh the terrain controls");
            })) return false;
        }
        return true;
    }

    protected override OnRefresh(): boolean {
        const status = this.view.FindElement("terrain-status");
        const statistics = this.view.FindElement("terrain-statistics");
        if (!status || !statistics) return false;
        for (const action of this.model.actions) {
            const button = this.view.FindElement(`terrain-${action}`);
            if (!button ||
                !button.SetEnabled(this.model.ready && this.model.pending === null) ||
                !button.SetClass("selected", action === this.model.camera)) return false;
        }
        return status.SetText(this.model.status) && statistics.SetText(this.model.statistics);
    }
}
