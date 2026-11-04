// Three.js r180 DFGApprox, MIT; see third_party/notices/threejs-reflections.md.
// Approximate split-sum coefficients used only when no baked BRDF LUT is bound.
#ifndef ENGINE_PBR_DFG_APPROX_SH
#define ENGINE_PBR_DFG_APPROX_SH
vec2 pbr_dfg_approx(float n_dot_v, float roughness)
{
    vec4 r = roughness * vec4(-1.0, -0.0275, -0.572, 0.022)
             + vec4(1.0, 0.0425, 1.04, -0.04);
    float a004 = min(r.x * r.x, exp2(-9.28 * n_dot_v)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}
#endif
