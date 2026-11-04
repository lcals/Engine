export const render_features = ["frustum_culling", "static_instancing", "static_lod"] as const;
export type RuntimeRenderFeature = typeof render_features[number];
export type RenderAction = RuntimeRenderFeature | "atmosphere" | "hex" | "camera";
export class RenderFeaturesModel {
    pending: RenderAction | null = null;
    status = "Preparing shared meshes and physical sky...";
    features: Record<RuntimeRenderFeature, boolean> = {
        frustum_culling: false, static_instancing: false, static_lod: false,
    };
    atmosphere = true;
    hex = false;
    distant = false;
    objects = 0;
}
