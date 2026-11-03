$input v_texcoord0
/*
 * InitialAdvanceRay / AdvanceRay / HierarchicalRaymarch / ValidateHit adapted
 * from Forge SSSR_Intersect.comp.fsl at 8278d8943f5da12918a990268fc18acdfbce9d9f.
 * Copyright (c) 2020 Advanced Micro Devices, Inc. MIT.
 * Copyright (c) 2017-2025 The Forge Interactive Inc. Apache-2.0.
 * Full licenses and changes: third_party/notices/forge-ssr.md.
 */
#include "bgfx_shader.sh"
#include "ssr_common.sh"
SAMPLER2D(s_ssr_scene, 0);
SAMPLER2D(s_ssr_ibl, 1);
SAMPLER2D(s_ssr_depth, 2);
SAMPLER2D(s_ssr_normal, 3);
SAMPLER2D(s_ssr_specular, 4);
SAMPLER2D(s_ssr_lut, 5);
SAMPLER2D(s_ssr_even, 6);
SAMPLER2D(s_ssr_odd, 7);

float hierarchy_depth(vec2 uv, int mip)
{
    return (mip & 1) == 0 ? texture2DLod(s_ssr_even, uv, float(mip)).r
                          : texture2DLod(s_ssr_odd, uv, float(mip)).r;
}

vec2 mip_dimensions(int mip)
{
    return max(floor(u_ssr_dimensions.xy * exp2(-float(mip))), vec2_splat(1.0));
}

bool hierarchical_raymarch(vec3 origin, vec3 direction, out vec3 hit)
{
    vec3 inverse_direction;
    inverse_direction.x = abs(direction.x) < 1e-8 ? 1e30 : 1.0 / direction.x;
    inverse_direction.y = abs(direction.y) < 1e-8 ? 1e30 : 1.0 / direction.y;
    inverse_direction.z = abs(direction.z) < 1e-8 ? 1e30 : 1.0 / direction.z;
    vec2 floor_offset = vec2(direction.x < 0.0 ? 0.0 : 1.0,
                             direction.y < 0.0 ? 0.0 : 1.0);
    vec2 uv_offset = vec2(direction.x < 0.0 ? -0.005 : 0.005,
                          direction.y < 0.0 ? -0.005 : 0.005) / u_ssr_dimensions.xy;
    vec2 boundary = (floor(origin.xy * u_ssr_dimensions.xy) + floor_offset) /
                    u_ssr_dimensions.xy + uv_offset;
    vec2 initial_t = (boundary - origin.xy) * inverse_direction.xy;
    float current_t = max(min(initial_t.x, initial_t.y), 0.0);
    hit = origin + current_t * direction;
    int mip = 0;
    for (int iteration = 0; iteration < 256; ++iteration)
    {
        if (iteration >= int(u_ssr_parameters.x) || current_t > 1.0 ||
            any(lessThan(hit, vec3_splat(0.0))) || any(greaterThan(hit, vec3_splat(1.0)))) return false;
        vec2 dimensions = mip_dimensions(mip);
        float surface_z = hierarchy_depth(hit.xy, mip);
        vec2 next_boundary = (floor(hit.xy * dimensions) + floor_offset) / dimensions + uv_offset;
        vec3 planes = vec3(next_boundary, surface_z);
        vec3 distances = (planes - origin) * inverse_direction;
        // This renderer uses ordinary depth: smaller values are nearer the camera.
        if (direction.z <= 0.0) distances.z = 1e30;
        float next_t = min(min(distances.x, distances.y), distances.z);
        bool above_surface = hit.z < surface_z;
        bool skipped_tile = next_t != distances.z && above_surface;
        if (above_surface)
        {
            current_t = max(next_t, current_t + 1e-7);
            hit = origin + current_t * direction;
        }
        if (!skipped_tile && mip == 0) return true;
        mip = clamp(mip + (skipped_tile ? 1 : -1), 0, int(u_ssr_frame.z) - 1);
    }
    return false;
}

float validate_hit(vec3 hit, vec3 source, vec3 reflected_direction)
{
    if (any(lessThan(hit, vec3_splat(0.0))) || any(greaterThan(hit, vec3_splat(1.0)))) return 0.0;
    float depth = texture2D(s_ssr_depth, hit.xy).r;
    if (depth >= 0.999999) return 0.0;
    vec3 hit_normal = texture2D(s_ssr_normal, hit.xy).xyz;
    if (dot(hit_normal, hit_normal) < 0.1) return 0.0;
    hit_normal = normalize(mul(u_ssr_view, vec4(hit_normal, 0.0)).xyz);
    if (dot(hit_normal, reflected_direction) >= 0.0) return 0.0;
    vec3 surface = ssr_view_position(hit.xy, depth);
    vec3 ray_hit = ssr_view_position(hit.xy, hit.z);
    float error = length(surface - ray_hit);
    float confidence = 1.0 - smoothstep(0.0, u_ssr_parameters.z, error);
    vec2 border_width = 0.05 * vec2(u_ssr_dimensions.y / u_ssr_dimensions.x, 1.0);
    vec2 border = smoothstep(vec2_splat(0.0), border_width, hit.xy) *
                  (1.0 - smoothstep(1.0 - border_width, vec2_splat(1.0), hit.xy));
    float traveled = length(surface - source);
    float range_fade = 1.0 - smoothstep(u_ssr_parameters.y * 0.8, u_ssr_parameters.y, traveled);
    return confidence * confidence * border.x * border.y * range_fade;
}

void main()
{
    gl_FragColor = vec4_splat(0.0);
    vec4 surface = texture2D(s_ssr_normal, v_texcoord0);
    vec4 specular = texture2D(s_ssr_specular, v_texcoord0);
    float depth = texture2D(s_ssr_depth, v_texcoord0).r;
    if (specular.a < 0.5 || depth >= 0.999999 || surface.w >= u_ssr_parameters.w ||
        dot(surface.xyz, surface.xyz) < 0.1) return;
    vec3 position = ssr_view_position(v_texcoord0, depth);
    vec3 normal = normalize(mul(u_ssr_view, vec4(normalize(surface.xyz), 0.0)).xyz);
    vec3 view_direction = u_ssr_frame.w > 0.5 ? vec3(0.0, 0.0, 1.0) : normalize(-position);
    vec3 direction = normalize(reflect(-view_direction, normal));
    vec3 start = position + normal * max(0.001, u_ssr_parameters.z * 0.02);
    float distance_limit = u_ssr_parameters.y;
    float near_depth = -ssr_view_position(vec2_splat(0.5), 0.0).z;
    if (direction.z > 0.0) distance_limit = min(distance_limit, (-near_depth - start.z) / direction.z * 0.99);
    if (distance_limit <= 0.001) return;
    vec3 screen_start = ssr_project(start);
    vec3 screen_end = ssr_project(start + direction * distance_limit);
    vec3 hit;
    if (!hierarchical_raymarch(screen_start, screen_end - screen_start, hit)) return;
    float confidence = validate_hit(hit, position, direction);
    confidence *= 1.0 - smoothstep(u_ssr_parameters.w * 0.7, u_ssr_parameters.w, surface.w);
    if (confidence <= 0.0) return;
    vec3 radiance = vec3_splat(0.0);
    float weight_sum = 0.0;
    float hit_depth = texture2D(s_ssr_depth, hit.xy).r;
    float hit_z = ssr_view_position(hit.xy, hit_depth).z;
    float radius = surface.w * surface.w * 12.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 sample_uv = clamp(hit.xy + vec2(float(x), float(y)) * radius / u_ssr_dimensions.xy,
                                    vec2_splat(0.0), vec2_splat(1.0));
            float sample_depth = texture2D(s_ssr_depth, sample_uv).r;
            float delta = abs(ssr_view_position(sample_uv, sample_depth).z - hit_z);
            float weight = exp2(-float(x * x + y * y)) * exp2(-delta / max(u_ssr_parameters.z, 0.001));
            radiance += texture2D(s_ssr_scene, sample_uv).rgb * weight;
            weight_sum += weight;
        }
    }
    radiance /= max(weight_sum, 0.0001);
    float n_dot_v = saturate(dot(normal, view_direction));
    vec2 lut;
    if (u_ssr_target.z > 0.5)
    {
        lut = texture2D(s_ssr_lut, vec2(n_dot_v, surface.w)).rg;
    }
    else
    {
        // Three.js r180 DFGApprox (MIT), 0af9729d0c143a86a1d725d6e2c3ad83301f3f34.
        // See third_party/notices/threejs-reflections.md. SSR remains useful before IBL baking.
        vec4 r = surface.w * vec4(-1.0, -0.0275, -0.572, 0.022)
                 + vec4(1.0, 0.0425, 1.04, -0.04);
        float a004 = min(r.x * r.x, exp2(-9.28 * n_dot_v)) * r.x + r.y;
        lut = vec2(-1.04, 1.04) * a004 + r.zw;
    }
    vec3 brdf = specular.rgb * lut.x + vec3_splat(lut.y);
    float attenuation = texture2D(s_ssr_ibl, v_texcoord0).a;
    gl_FragColor = vec4(clamp(radiance * brdf * attenuation, vec3_splat(0.0), vec3_splat(65504.0)), confidence);
}
