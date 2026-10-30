import { UiController } from "engine";
import { LauncherModel } from "../models/launcher_model.js";

export class LauncherController extends UiController<LauncherModel> {
    protected override OnBind(): boolean {
        if (this.model.selected) {
            const back = this.view.FindElement("back-menu");
            return back !== null && this.Listen(back, "click", () => {
                if (this.model.pending !== null) return;
                this.model.pending = "menu";
                if (!this.Refresh()) throw new Error("Cannot refresh the launcher view");
            });
        }
        for (const choice of this.model.choices) {
            const button = this.view.FindElement(choice.button);
            if (!button || !this.Listen(button, "click", () => {
                if (this.model.pending !== null) return;
                this.model.pending = choice;
                this.model.error = null;
                if (!this.Refresh()) throw new Error("Cannot refresh the launcher view");
            })) return false;
        }
        return true;
    }

    protected override OnRefresh(): boolean {
        const root = this.view.FindElement("launcher-root");
        const page = this.view.FindElement("launcher-page");
        const back = this.view.FindElement("back-menu");
        const status = this.view.FindElement("launcher-status");
        if (!root || !page || !back || !status) return false;
        const in_world = this.model.selected !== null;
        const pending = this.model.pending;
        if (
            !root.SetStyle("background-color", in_world ? "#00000000" : "#0e1726") ||
            !root.SetStyle("pointer-events", in_world ? "none" : "auto") ||
            !root.SetStyle("overflow-y", in_world ? "hidden" : "auto") ||
            !page.SetStyle("display", in_world ? "none" : "block") ||
            !back.SetStyle("display", in_world ? "block" : "none") ||
            !back.SetEnabled(pending === null)
        ) return false;
        for (const choice of this.model.choices) {
            const button = this.view.FindElement(choice.button);
            if (!button || !button.SetEnabled(pending === null)) return false;
        }
        return status.SetText(this.model.error ?? (pending && pending !== "menu"
            ? `Opening ${pending.title}...`
            : "Open a scene. Mouse, touch and keyboard share the same controller."));
    }
}
