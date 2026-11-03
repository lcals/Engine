import { UiController } from "engine";
import { ecs_actions, EcsManyObjectsModel } from "../models/ecs_many_objects_model.js";

export class EcsManyObjectsController extends UiController<EcsManyObjectsModel> {
    protected override OnBind(): boolean {
        for (const action of ecs_actions) {
            const button = this.view.FindElement(`ecs-${action}`);
            if (!button || !this.Listen(button, "click", () => { this.model.pending = action; })) return false;
        }
        return true;
    }

    protected override OnRefresh(): boolean {
        return (this.view.FindElement("ecs-status")?.SetText(this.model.status) ?? false) &&
            (this.view.FindElement("ecs-counts")?.SetText(
                `Objects ${this.model.created} / ${this.model.requested} | Checked ${this.model.checked}`) ?? false) &&
            (this.view.FindElement("ecs-state")?.SetText(
                `Visible ${this.model.visible} | Active ${this.model.active} | Generations ${this.model.generations}`) ?? false) &&
            (this.view.FindElement("ecs-handles")?.SetText(
                `Retired handles rejected: ${this.model.stale_handles}. Each object has a shared mesh and independent ECS state.`) ?? false);
    }
}
