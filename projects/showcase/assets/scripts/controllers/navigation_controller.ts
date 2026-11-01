import { UiController } from "engine";
import { NavigationModel } from "../models/navigation_model.js";

export class NavigationController extends UiController<NavigationModel> {
    protected override OnBind(): boolean {
        for (const action of this.model.actions) {
            const button = this.view.FindElement(`navigation-${action}`);
            if (
                !button ||
                !this.Listen(button, "click", () => {
                    if (!this.model.ready || this.model.pending !== null) return;
                    this.model.pending = action;
                    if (!this.Refresh()) throw new Error("Cannot update navigation controls");
                })
            )
                return false;
        }
        return true;
    }

    protected override OnRefresh(): boolean {
        const status = this.view.FindElement("navigation-status");
        const statistics = this.view.FindElement("navigation-statistics");
        const terrainStatistics = this.view.FindElement("navigation-terrain-statistics");
        if (!status || !statistics || !terrainStatistics) return false;
        for (const action of this.model.actions) {
            const button = this.view.FindElement(`navigation-${action}`);
            const selected =
                action === this.model.mode ||
                action === this.model.camera ||
                action === `count${this.model.count}` ||
                (action === "reverse" && this.model.reversed);
            if (
                !button ||
                !button.SetEnabled(
                    this.model.ready &&
                        this.model.pending === null &&
                        (this.model.synchronized ||
                            action === "restore" ||
                            action === "retryTerrain" ||
                            action === "overview" ||
                            action === "detail") &&
                        (action !== "stop" || !this.model.stopped),
                ) ||
                !button.SetClass("selected", selected)
            )
                return false;
        }
        return (
            status.SetText(this.model.status) &&
            statistics.SetText(this.model.statistics) &&
            terrainStatistics.SetText(this.model.terrainStatistics)
        );
    }
}
