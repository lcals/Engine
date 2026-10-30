import { UiController } from "engine";
import { LauncherModel } from "../models/launcher_model.js";

export class LauncherController extends UiController<LauncherModel> {
    protected override OnBind(): boolean {
        for (const choice of this.model.choices) {
            const button = this.view.FindElement(choice.button);
            if (!button || !this.Listen(button, "click", () => {
                this.model.selected = choice;
                this.model.selection_count += 1;
                if (!this.Refresh()) throw new Error("Cannot refresh the launcher view");
            })) return false;
        }
        return true;
    }

    protected override OnRefresh(): boolean {
        const status = this.view.FindElement("launcher-status");
        if (!status) return false;
        const selected = this.model.selected;
        return status.SetText(selected
            ? `Selected: ${selected.title} / ${this.model.selection_count} selections`
            : "Choose a scene. Mouse, touch and keyboard share the same controller.");
    }
}
