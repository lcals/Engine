import { UiController } from "engine";
import { lifetime_actions, PersistentUiModel } from "../models/persistent_ui_model.js";

export class PersistentUiController extends UiController<PersistentUiModel> {
    protected override OnBind(): boolean {
        for (const action of lifetime_actions) {
            const button = this.view.FindElement(action);
            if (!button || !this.Listen(button, "click", () => {
                this.model.pending = action;
                ++this.model.events;
            })) return false;
        }
        const note = this.view.FindElement("lifetime-note");
        return !!note && this.Listen(note, "input", () => { ++this.model.events; });
    }

    protected override OnRefresh(): boolean {
        return (this.view.FindElement("lifetime-status")?.SetText(this.model.status) ?? false) &&
            (this.view.FindElement("lifetime-identity")?.SetText(this.model.identity) ?? false) &&
            (this.view.FindElement("lifetime-counts")?.SetText(
                `Scene commits: ${this.model.completed} / UI events: ${this.model.events}`) ?? false);
    }
}
