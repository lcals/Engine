$input v_world_position, v_world_normal, v_world_tangent, v_texcoord0, v_color0, v_lightmap_uv

/*
 * Lambert/Blinn-Phong adapted from bgfx examples/18-ibl/fs_ibl_mesh.sc:
 * Copyright 2014-2016 Dario Manesku. BSD-2-Clause.
 * Exponential fog adapted from examples/16-shadowmaps/fs_shadowmaps_color_lighting_main.sh.
 * Copyright 2013-2014 Dario Manesku. BSD-2-Clause.
 * License and attribution: third_party/notices/bgfx.txt.
 * Changes: empirical unnormalized highlight, exponent bias 1, named texture parameters;
 * fog uses configurable density, forward camera depth, linear color and a negative exponent.
 * Image-based lighting adapts Ant's pbr/ibl.sh split-sum sampling and bgfx 18-ibl
 * environment-map combination. Ant copyright 2017-2024 lingxigames.com, MIT.
 * Linear HDR maps, explicit resource slots and the existing material workflow are retained.
 * See third_party/notices/ant.txt for the Ant license.
 */

#include <bgfx_shader.sh>
#include "pbr_helpers.sh"
#include "material_helpers.sh"
#include "shadow_helpers.sh"
#include "csm_helpers.sh"

#include "pbr_surface.sh"
#include "cluster/lighting.sh"
#include "local_shadow_helpers.sh"
SAMPLER2D(s_lightmap, 6);
SAMPLER2D(s_ambient_occlusion, 11);
SAMPLERCUBE(s_ibl_irradiance, 12);
SAMPLERCUBE(s_ibl_prefilter, 13);
SAMPLER2D(s_ibl_lut, 14);
uniform vec4 u_ibl_params;
uniform vec4 u_ambient_occlusion;
uniform vec4 u_lighting_params;
uniform vec4 u_probe_sh[9];
uniform vec4 u_capture_mode;
uniform vec4 u_camera_position;
uniform vec4 u_ambient;
uniform vec4 u_ambient_sky;
uniform vec4 u_ambient_equator;
uniform vec4 u_ambient_ground;
uniform vec4 u_fog_color;
// x: linear start, y: linear end, z: exponential density, w: camera near plane.
uniform vec4 u_fog_range;
// Exposure, ACES enabled, linear HDR output enabled, reserved.
uniform vec4 u_tonemap;

// SH9 irradiance, real basis ordered exactly as the CPU probe resource.
vec3 probe_irradiance(vec3 n)
{
    return max(vec3_splat(0.0), u_probe_sh[0].rgb * (0.2820947918 * 3.1415926536)
        + u_probe_sh[1].rgb * 2.0943951024 * (0.4886025119 * n.y)
        + u_probe_sh[2].rgb * 2.0943951024 * (0.4886025119 * n.z)
        + u_probe_sh[3].rgb * 2.0943951024 * (0.4886025119 * n.x)
        + u_probe_sh[4].rgb * 0.7853981634 * (1.0925484306 * n.x * n.y)
        + u_probe_sh[5].rgb * 0.7853981634 * (1.0925484306 * n.y * n.z)
        + u_probe_sh[6].rgb * 0.7853981634 * (0.3153915653 * (3.0 * n.z * n.z - 1.0))
        + u_probe_sh[7].rgb * 0.7853981634 * (1.0925484306 * n.x * n.z)
        + u_probe_sh[8].rgb * 0.7853981634 * (0.5462742153 * (n.x * n.x - n.y * n.y)));
}

#include "pbr_lighting.sh"
#include "dfg_approx.sh"

void main()
{
    vec3 world_dx = dFdx(v_world_position);
    vec3 world_dy = dFdy(v_world_position);
    vec3 view_offset = u_camera_position.xyz - v_world_position;
    vec3 view_dir = pbr_view_direction(u_camera_position.xyz, v_world_position);
    PbrSurface surface = sample_pbr_surface(v_texcoord0, v_color0, v_world_normal,
                                          v_world_tangent, view_dir);
    vec3 color = surface.base_color.rgb;
    vec3 environment_specular = vec3_splat(0.0);
    float reflection_attenuation = 0.0;
    bool unlit = u_material.z > 0.5 && u_material.z < 1.5;
    if (!unlit)
    {
        vec3 ambient_color = u_ambient.rgb;
        if (u_ambient_sky.w > 0.5)
        {
            float height = clamp(surface.normal.y, -1.0, 1.0);
            ambient_color = u_ambient_sky.rgb * max(height, 0.0)
                          + u_ambient_ground.rgb * max(-height, 0.0)
                          + u_ambient_equator.rgb * (1.0 - abs(height));
        }
        vec3 diffuse_irradiance = ambient_color * u_ambient.w;
        vec3 diffuse_factor = vec3_splat(surface.diffuse_weight);
        vec3 environment_lighting = vec3_splat(0.0);
        if (u_ibl_params.x > 0.5 && u_capture_mode.x < 0.5)
        {
            diffuse_irradiance += textureCube(s_ibl_irradiance, surface.normal).rgb * u_ibl_params.y;
            vec3 reflection = reflect(-view_dir, surface.normal);
            vec3 prefiltered = textureCubeLod(s_ibl_prefilter, reflection,
                                              surface.roughness * u_ibl_params.z).rgb;
            vec2 lut = texture2D(s_ibl_lut, vec2(saturate(dot(surface.normal, view_dir)),
                                               surface.roughness)).rg;
            environment_specular = prefiltered * (surface.specular_color * lut.x + vec3_splat(lut.y))
                                   * u_ibl_params.y;
            environment_lighting = environment_specular;
            if (u_specular.w < 0.5 && u_material.z < 1.5)
            {
                // glTF mixes the complete dielectric and metal lobes, not their F0
                // before the nonlinear multi-scattering approximation.
                float n_dot_v = saturate(dot(surface.normal, view_dir));
                vec3 dielectric = pbr_ibl_fresnel(vec3_splat(0.04), surface.roughness, n_dot_v, lut);
                vec3 metal = pbr_ibl_fresnel(surface.base_color.rgb, surface.roughness, n_dot_v, lut);
                diffuse_factor = (vec3_splat(1.0) - dielectric) * (1.0 - surface.metallic);
                environment_lighting = prefiltered * mix(dielectric, metal, surface.metallic)
                                       * u_ibl_params.y;
                // MRT keeps the linear split-sum subterm for SSR replacement, not
                // the full Khronos single and multiple scattering contribution.
                // The remaining environment approximation stays in the main color.
            }
            else if (u_specular.w > 0.5 && u_material.z < 1.5)
            {
                // The SpecGloss c_diff already includes (1 - max(F0)). Apply
                // the same view-dependent IBL Fresnel mix to that diffuse input.
                float n_dot_v = saturate(dot(surface.normal, view_dir));
                vec3 fresnel = pbr_ibl_fresnel(surface.specular_color, surface.roughness,
                                               n_dot_v, lut);
                diffuse_factor = (vec3_splat(1.0) - fresnel) * surface.diffuse_weight;
                environment_lighting = prefiltered * fresnel * u_ibl_params.y;
            }
        }
        else if (u_capture_mode.x < 0.5 && u_material.z < 1.5)
        {
            // Diffuse-only ambient/lightmap/probe still loses energy to the
            // dielectric interface, even when its reflected radiance is absent.
            // This fallback uses Three.js' approximate DFG, not a baked LUT.
            float n_dot_v = saturate(dot(surface.normal, view_dir));
            vec2 lut = pbr_dfg_approx(n_dot_v, surface.roughness);
            vec3 f0 = u_specular.w > 0.5 ? surface.specular_color : vec3_splat(0.04);
            vec3 fresnel = clamp(pbr_ibl_fresnel(f0, surface.roughness, n_dot_v, lut),
                                 vec3_splat(0.0), vec3_splat(1.0));
            diffuse_factor = (vec3_splat(1.0) - fresnel) * surface.diffuse_weight;
        }
        if (u_lighting_params.x > 0.5)
        {
            diffuse_irradiance = texture2D(s_lightmap, v_lightmap_uv).rgb / 3.14159265;
        }
        else if (u_lighting_params.y > 0.5)
        {
            diffuse_irradiance = probe_irradiance(surface.normal) / 3.14159265;
        }
        color = surface.base_color.rgb * diffuse_factor * diffuse_irradiance;
        float ambient_occlusion = 1.0;
        if (u_ambient_occlusion.z > 0.5)
        {
            ambient_occlusion = texture2D(s_ambient_occlusion,
                                           gl_FragCoord.xy * u_ambient_occlusion.xy).r;
        }
        // glTF 2.0 occlusion affects indirect illumination, not direct lights.
        float indirect_occlusion = ambient_occlusion * surface.occlusion;
        color = (color + environment_lighting) * indirect_occlusion;
        environment_specular *= indirect_occlusion;
        reflection_attenuation = indirect_occlusion;
        float view_depth = max(-mul(u_view, vec4(v_world_position, 1.0)).z, 0.0);
        // Directional lights are global; the cluster list only stores bounded local lights.
        for (uint index = 0u; index < uint(u_cluster_counts.x); ++index)
        {
            if (load_cluster_light(index).kind_inner.x < 0.5)
            {
                color += direct_light(index, v_world_position, world_dx, world_dy,
                                      surface.normal, v_world_normal, view_dir, surface.base_color.rgb,
                                      surface.metallic, surface.roughness, surface.specular_color,
                                      surface.smoothness);
            }
        }
        uint offset; uint count; bool overflow;
        cluster_range(gl_FragCoord.xy, view_depth, offset, count, overflow);
        for (uint item = 0u; item < count; ++item)
        {
            uint index = cluster_light_index(offset, item, overflow);
            if (load_cluster_light(index).kind_inner.x > 0.5)
            {
                color += direct_light(index, v_world_position, world_dx, world_dy,
                                      surface.normal, v_world_normal, view_dir, surface.base_color.rgb,
                                      surface.metallic, surface.roughness, surface.specular_color,
                                      surface.smoothness);
            }
        }
    }
    color += surface.emission;
    if (u_capture_mode.x > 0.5)
    {
        float valid = dot(v_world_normal, view_dir) > 0.0 ? 1.0 : 0.0;
        gl_FragData[0] = vec4(clamp(color, vec3_splat(0.0), vec3_splat(65504.0)), valid);
        gl_FragData[1] = vec4_splat(0.0);
        return;
    }
    if (u_fog_color.w > 0.5)
    {
        float view_distance = pbr_orthographic_view()
            ? max(-mul(u_view, vec4(v_world_position, 1.0)).z, 0.0) : length(view_offset);
        float fog = saturate((view_distance - u_fog_range.x)
                             / max(u_fog_range.y - u_fog_range.x, 0.0001));
        if (u_fog_color.w > 1.5)
        {
            float depth = max(-mul(u_view, vec4(v_world_position, 1.0)).z - u_fog_range.w, 0.0);
            float optical_depth = min(u_fog_range.z * depth, 80.0);
            float exponent = u_fog_color.w > 2.5 ? optical_depth * optical_depth : optical_depth;
            fog = 1.0 - exp2(-exponent * 1.4426950408889634);
        }
        float transmission = 1.0 - fog * u_surface_parameters.z;
        color = mix(u_fog_color.rgb, color, transmission);
        environment_specular *= transmission;
        reflection_attenuation *= transmission;
    }
    float output_alpha = u_surface.y > 0.5 ? surface.base_color.a : (unlit ? 0.0 : 1.0);
    if (u_surface.y > 1.5) color *= output_alpha;
    gl_FragData[0] = vec4(clamp(color, vec3_splat(0.0), vec3_splat(65504.0)), output_alpha);
    gl_FragData[1] = vec4(environment_specular, reflection_attenuation);
}
