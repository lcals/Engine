import { UiController, Window } from "engine";
import { LauncherModel } from "../models/launcher_model.js";

export class LauncherController extends UiController<LauncherModel> {
    protected override OnBind(): boolean {
        const back = this.view.FindElement("back-menu");
        if (!back || !this.Listen(back, "click", () => {
            if (this.model.pending !== null) return;
            this.model.pending = "menu";
            if (!this.Refresh()) throw new Error("Cannot refresh the launcher view");
        })) return false;
        const movie = this.view.FindElement("replay-intro");
        if (!movie || !this.Listen(movie, "click", () => {
            if (this.model.pending !== null || this.model.movie_playing) return;
            this.model.movie_requested = true;
        })) return false;
        for (const choice of this.model.choices) {
            const button = this.view.FindElement(choice.button);
            const title = this.view.FindElement(`${choice.button}-title`);
            const goal = this.view.FindElement(`${choice.button}-goal`);
            if (
                !button || !title?.SetText(choice.title) || !goal?.SetText(choice.goal) ||
                !button.SetText(`Test ${choice.title}`) ||
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
        const movie = this.view.FindElement("replay-intro");
        if (!root || !back || !status || !movie ||
            !movie.SetEnabled(this.model.pending === null && !this.model.movie_playing)) return false;
        const size = Window.GetSize();
        const safe = Window.GetSafeArea();
        if (size && safe) {
            const right = Math.max(0, size.logicalWidth - safe.x - safe.width);
            const bottom = Math.max(0, size.logicalHeight - safe.y - safe.height);
            if (!root.SetStyle("box-sizing", "border-box") ||
                !root.SetStyle("padding", `${safe.y}px ${right}px ${bottom}px ${safe.x}px`) ||
                !back.SetStyle("top", `${24 + safe.y}px`) ||
                !back.SetStyle("left", `${24 + safe.x}px`)) return false;
        }
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
