$input v_world_position, v_world_normal, v_world_tangent, v_texcoord0, v_color0, v_lightmap_uv

/*
 * Planar reflection and dual scrolling normals adapt Three.js Water2.js;
 * Beer-Lambert transmission adapts transmission_pars_fragment.glsl.js.
 * three.js bf8f297466d0a8f8ef141d83113c79fadbe1019c, MIT.
 * Copyright 2010-2026 three.js authors. See third_party/notices/threejs-reflections.md.
 * Changes: bgfx input contracts, scene-depth rejection, Forward+ lighting and shadows.
 */
#include <bgfx_shader.sh>
#include "pbr_helpers.sh"
#include "pbr_surface.sh"
#include "shadow_helpers.sh"
#include "csm_helpers.sh"
#include "cluster/lighting.sh"
#include "local_shadow_helpers.sh"

SAMPLER2D(s_planar_reflection, 8);
SAMPLER2D(s_scene_color, 9);
SAMPLER2D(s_scene_depth, 10);
SAMPLERCUBE(s_ibl_prefilter, 13);
uniform vec4 u_camera_position;
uniform vec4 u_capture_mode;
uniform vec4 u_ibl_params;
uniform vec4 u_frame_inputs;
uniform vec4 u_frame_time;
uniform vec4 u_frame_valid;
uniform mat4 u_planar_projection;
uniform vec4 u_fog_color;
uniform vec4 u_fog_range;
// xy: flow speed, z: index of refraction, w: unused.
uniform vec4 u_water_flow;
// x: refraction strength, y: absorption distance, z: shore fade, w: reflection strength.
uniform vec4 u_water_parameters;
uniform vec4 u_water_absorption;
#include "pbr_lighting.sh"

vec3 water_world_position(vec2 uv, float depth)
{
    vec2 ndc = uv * 2.0 - 1.0;
    if (u_frame_inputs.w < 0.5) ndc.y = -ndc.y;
    vec4 position = mul(u_invViewProj, vec4(ndc, depth, 1.0));
    return position.xyz / position.w;
}

void main()
{
    vec3 view_offset = u_camera_position.xyz - v_world_position;
    vec3 view_dir = pbr_view_direction(u_camera_position.xyz, v_world_position);
    PbrSurface surface = sample_pbr_surface(v_texcoord0, v_color0, v_world_normal,
                                           v_world_tangent, view_dir);
    vec2 uv = v_texcoord0 * u_uv_transform.xy + u_uv_transform.zw;
    vec2 flow = u_water_flow.xy * u_frame_time.x;
    // Independent opposing samples hide the periodic translation of a single normal map.
    vec3 normal_a = texture2D(s_normal, uv + flow).xyz * 2.0 - 1.0;
    vec3 normal_b = texture2D(s_normal, uv * 1.37 - flow * 0.73 + vec2(0.31, 0.67)).xyz * 2.0 - 1.0;
    vec3 normal_sample = normalize(vec3((normal_a.xy + normal_b.xy) * u_surface_parameters.y,
                                       max(normal_a.z + normal_b.z, 0.01)));
    vec3 normal = normalize(v_world_normal);
    vec3 tangent = normalize(v_world_tangent.xyz - normal * dot(normal, v_world_tangent.xyz));
    vec3 bitangent = cross(normal, tangent) * v_world_tangent.w;
    normal = normalize(tangent * normal_sample.x + bitangent * normal_sample.y + normal * normal_sample.z);
    if (u_surface.x > 0.5 && dot(v_world_normal, view_dir) < 0.0) normal = -normal;
    vec2 screen_uv = gl_FragCoord.xy * u_frame_inputs.xy;
    float surface_depth = max(-mul(u_view, vec4(v_world_position, 1.0)).z, 0.0001);
    float water_distance = u_water_parameters.y;
    if (u_frame_valid.y > 0.5)
    {
        float depth = texture2D(s_scene_depth, screen_uv).r;
        vec3 world = water_world_position(screen_uv, depth);
        water_distance = pbr_orthographic_view()
            ? max(dot(world - v_world_position, -view_dir), 0.0)
            : max(length(world - u_camera_position.xyz) - length(view_offset), 0.0);
    }
    // Three.js getVolumeTransmissionRay / getIBLVolumeRefraction: refract in world
    // space and project the exit point. Our thickness comes from scene depth, not
    // a separate mesh thickness map. IOR=1 projects back to the original pixel.
    vec3 geometric_normal = normalize(v_world_normal);
    vec3 transmission_direction = normalize(refract(-view_dir, normal, 1.0 / u_water_flow.z));
    float thickness = water_distance * abs(dot(view_dir, geometric_normal));
    float transmission_distance = thickness /
                                  max(abs(dot(transmission_direction, geometric_normal)), 0.05);
    vec4 exit_clip = mul(u_viewProj, vec4(v_world_position +
                         transmission_direction * transmission_distance, 1.0));
    vec2 refraction_uv = screen_uv;
    if (exit_clip.w > 0.0001)
    {
        vec2 projected_uv = exit_clip.xy / exit_clip.w * 0.5 + 0.5;
        if (u_frame_inputs.w < 0.5) projected_uv.y = 1.0 - projected_uv.y;
        // Offscreen information is unavailable; use the undistorted background.
        if (all(greaterThanEqual(projected_uv, vec2_splat(0.0))) &&
            all(lessThanEqual(projected_uv, vec2_splat(1.0))))
        {
            refraction_uv = mix(screen_uv, projected_uv, u_water_parameters.x);
        }
    }
    refraction_uv = clamp(refraction_uv, u_frame_inputs.xy * 0.5,
                          vec2_splat(1.0) - u_frame_inputs.xy * 0.5);
    if (u_frame_valid.y > 0.5)
    {
        vec3 candidate = water_world_position(refraction_uv, texture2D(s_scene_depth, refraction_uv).r);
        float candidate_depth = -mul(u_view, vec4(candidate, 1.0)).z;
        // Foreground geometry must not be pulled into the water by distorted screen UVs.
        if (candidate_depth < surface_depth) refraction_uv = screen_uv;
    }
    vec3 transmitted = surface.base_color.rgb;
    if (u_frame_valid.x > 0.5) transmitted = texture2D(s_scene_color, refraction_uv).rgb;
    vec3 attenuation = exp(log(max(u_water_absorption.rgb, vec3_splat(0.0001))) *
                           min(transmission_distance / max(u_water_parameters.y, 0.001), 80.0));
    vec3 reflection = vec3_splat(0.0);
    if (u_ibl_params.x > 0.5)
    {
        reflection = textureCubeLod(s_ibl_prefilter, reflect(-view_dir, normal),
                                     surface.roughness * u_ibl_params.z).rgb * u_ibl_params.y;
    }
    if (u_frame_valid.z > 0.5)
    {
        vec4 projected = mul(u_planar_projection, vec4(v_world_position, 1.0));
        vec2 reflected_uv = projected.xy / max(projected.w, 0.0001) * 0.5 + 0.5;
        if (u_frame_inputs.w < 0.5) reflected_uv.y = 1.0 - reflected_uv.y;
        reflected_uv += normal_sample.xy * (0.025 * u_water_parameters.x);
        if (projected.w > 0.0 && all(greaterThanEqual(reflected_uv, vec2_splat(0.0))) &&
            all(lessThanEqual(reflected_uv, vec2_splat(1.0))))
        {
            reflection = texture2D(s_planar_reflection, reflected_uv).rgb;
        }
    }
    float ior_ratio = (u_water_flow.z - 1.0) / (u_water_flow.z + 1.0);
    float f0 = ior_ratio * ior_ratio;
    float fresnel = (f0 + (1.0 - f0) * pow(1.0 - saturate(dot(normal, view_dir)), 5.0)) *
                     u_water_parameters.w;
    vec3 color = (transmitted * attenuation + surface.base_color.rgb * (vec3_splat(1.0) - attenuation)) *
                 (1.0 - fresnel) + reflection * fresnel * surface.occlusion;
    for (uint index = 0u; index < uint(u_cluster_counts.x); ++index)
    {
        if (load_cluster_light(index).kind_inner.x < 0.5)
        {
            color += direct_light(index, v_world_position, normal, v_world_normal, view_dir, vec3_splat(0.0),
                                  surface.metallic, surface.roughness, vec3_splat(f0),
                                  surface.smoothness);
        }
    }
    uint offset; uint count; bool overflow;
    cluster_range(gl_FragCoord.xy, surface_depth, offset, count, overflow);
    for (uint item = 0u; item < count; ++item)
    {
        uint index = cluster_light_index(offset, item, overflow);
        if (load_cluster_light(index).kind_inner.x > 0.5)
        {
            color += direct_light(index, v_world_position, normal, v_world_normal, view_dir, vec3_splat(0.0),
                                  surface.metallic, surface.roughness, vec3_splat(f0),
                                  surface.smoothness);
        }
    }
    color += surface.emission;
    if (u_fog_color.w > 0.5)
    {
        float view_distance = pbr_orthographic_view() ? surface_depth : length(view_offset);
        float fog = saturate((view_distance - u_fog_range.x) /
                            max(u_fog_range.y - u_fog_range.x, 0.0001));
        if (u_fog_color.w > 1.5)
        {
            float optical_depth = min(u_fog_range.z * max(surface_depth - u_fog_range.w, 0.0), 80.0);
            float exponent = u_fog_color.w > 2.5 ? optical_depth * optical_depth : optical_depth;
            fog = 1.0 - exp2(-exponent * 1.4426950408889634);
        }
        color = mix(color, u_fog_color.rgb, fog * u_surface_parameters.z);
    }
    float coverage = surface.base_color.a * saturate(water_distance / max(u_water_parameters.z, 0.001));
    if (u_surface.y > 1.5) color *= coverage;
    gl_FragColor = vec4(clamp(color, vec3_splat(0.0), vec3_splat(65504.0)), coverage);
}
