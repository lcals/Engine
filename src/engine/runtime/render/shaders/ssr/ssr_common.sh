/* Deterministic SSR adaptation; see third_party/notices/forge-ssr.md. */
uniform vec4 u_ssr_dimensions; // full width/height, trace width/height
uniform vec4 u_ssr_parameters; // max steps, max distance, thickness, roughness cutoff
uniform vec4 u_ssr_frame; // originBottomLeft, homogeneousDepth, mip count, orthographic
uniform vec4 u_ssr_reduce; // source width/height, source mip, copy depth
uniform vec4 u_ssr_target; // destination width/height, BRDF LUT valid
uniform mat4 u_ssr_view;
uniform mat4 u_ssr_projection;
uniform mat4 u_ssr_inverse_projection;

vec3 ssr_view_position(vec2 uv, float depth)
{
    vec2 ndc = uv * 2.0 - 1.0;
    if (u_ssr_frame.x < 0.5) ndc.y = -ndc.y;
    float z = u_ssr_frame.y > 0.5 ? depth * 2.0 - 1.0 : depth;
    vec4 p = mul(u_ssr_inverse_projection, vec4(ndc, z, 1.0));
    return p.xyz / p.w;
}

vec3 ssr_project(vec3 position)
{
    vec4 clip = mul(u_ssr_projection, vec4(position, 1.0));
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (u_ssr_frame.x < 0.5) uv.y = 1.0 - uv.y;
    float depth = u_ssr_frame.y > 0.5 ? ndc.z * 0.5 + 0.5 : ndc.z;
    return vec3(uv, depth);
}
