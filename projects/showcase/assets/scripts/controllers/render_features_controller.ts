import { UiController } from "engine";
import { render_features, RenderFeaturesModel } from "../models/render_features_model.js";

export class RenderFeaturesController extends UiController<RenderFeaturesModel> {
    protected override OnBind(): boolean {
        for (const action of [...render_features, "atmosphere", "hex", "camera"] as const) {
            const element = this.view.FindElement(`render-${action}`);
            if (!element || !this.Listen(element, "click", () => { this.model.pending = action; })) return false;
        }
        return true;
    }
    protected override OnRefresh(): boolean {
        for (const feature of render_features) {
            const text = `${feature}: ${this.model.features[feature] ? "ON" : "OFF"} (component property)`;
            if (!this.view.FindElement(`render-${feature}`)?.SetText(text)) return false;
        }
        return !!this.view.FindElement("render-status")?.SetText(this.model.status) &&
            !!this.view.FindElement("render-counts")?.SetText(`${this.model.objects} shared meshes. Read actual submission statistics and control diagnostics through MCP.`) &&
            !!this.view.FindElement("render-atmosphere")?.SetText(`Atmosphere: ${this.model.atmosphere ? "ON" : "OFF"}`) &&
            !!this.view.FindElement("render-hex")?.SetText(`Hex tiling: ${this.model.hex ? "ON" : "OFF"}`) &&
            !!this.view.FindElement("render-camera")?.SetText(this.model.distant ? "Camera: far" : "Camera: near");
    }
}
