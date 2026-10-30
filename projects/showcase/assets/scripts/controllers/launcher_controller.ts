import { UiController } from "engine";
import { LauncherModel } from "../models/launcher_model.js";

export class LauncherController extends UiController<LauncherModel> {
    protected override OnBind(): boolean {
        if (this.model.selected) {
            const back = this.view.FindElement("back-menu");
            return (
                back !== null &&
                this.Listen(back, "click", () => {
                    if (this.model.pending !== null) return;
                    this.model.pending = "menu";
                    if (!this.Refresh()) throw new Error("Cannot refresh the launcher view");
                })
            );
        }
        for (const choice of this.model.choices) {
            const button = this.view.FindElement(choice.button);
            if (
                !button ||
                !this.Listen(button, "click", () => {
                    if (this.model.pending !== null) return;
                    this.model.pending = choice;
                    this.model.error = null;
                    if (!this.Refresh()) throw new Error("Cannot refresh the launcher view");
                })
            )
                return false;
        }
        return true;
    }

    protected override OnRefresh(): boolean {
        const root = this.view.FindElement("launcher-root");
        const back = this.view.FindElement("back-menu");
        const status = this.view.FindElement("launcher-status");
        if (!root || !back || !status) return false;
        const in_world = this.model.selected !== null;
        const pending = this.model.pending;
        if (!root.SetClass("in-world", in_world) || !back.SetEnabled(pending === null))
            return false;
        for (const choice of this.model.choices) {
            const button = this.view.FindElement(choice.button);
            if (!button || !button.SetEnabled(pending === null)) return false;
        }
        return status.SetText(
            this.model.error ??
                (pending && pending !== "menu"
                    ? `Opening ${pending.title}...`
                    : "Open a scene. Mouse, touch and keyboard share the same controller."),
        );
    }
}
