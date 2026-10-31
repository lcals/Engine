$input v_world_position, v_world_normal, v_world_tangent, v_texcoord0, v_color0

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

SAMPLER2D(s_base_color, 0);
SAMPLER2D(s_normal, 1);
SAMPLER2D(s_orm, 2);
SAMPLER2D(s_specular, 3);
SAMPLER2D(s_emission, 4);
SAMPLER2D(s_ambient_occlusion, 11);
SAMPLERCUBE(s_ibl_irradiance, 12);
SAMPLERCUBE(s_ibl_prefilter, 13);
SAMPLER2D(s_ibl_lut, 14);
// enabled, intensity, last prefiltered mip, reserved.
uniform vec4 u_ibl_params;
// Reciprocal window dimensions (xy), screen-space AO enabled (z), reserved (w).
uniform vec4 u_ambient_occlusion;
uniform vec4 u_base_color;
// x: metallic, y: roughness, z: lighting (0 PBR, 1 unlit, 2 Blinn-Phong), w: alpha cutoff.
uniform vec4 u_material;
// x: back faces can be visible, remaining values reserved.
uniform vec4 u_surface;
// rgb: specular reflectance multiplier, w: specular workflow enabled.
uniform vec4 u_specular;
// x: smoothness source (0 specular alpha * smoothness, 1 base alpha), y: direct highlights.
uniform vec4 u_specular_options;
uniform vec4 u_emission;
// x: smoothness multiplier, y: normal scale, z: fog influence, w: reserved.
uniform vec4 u_surface_parameters;
// xy: scale, zw: offset, shared by every surface texture and shadow coverage.
uniform vec4 u_uv_transform;
uniform vec4 u_camera_position;
uniform vec4 u_light_count;
uniform vec4 u_light_position_range[64];
uniform vec4 u_light_direction_outer[64];
uniform vec4 u_light_color_intensity[64];
uniform vec4 u_light_kind_inner[64];
uniform vec4 u_ambient;
uniform vec4 u_ambient_sky;
uniform vec4 u_ambient_equator;
uniform vec4 u_ambient_ground;
uniform vec4 u_fog_color;
// x: linear start, y: linear end, z: exponential density, w: camera near plane.
uniform vec4 u_fog_range;
// Exposure, ACES enabled, linear HDR output enabled, reserved.
uniform vec4 u_tonemap;

vec3 indirect_light(vec3 normal, vec3 view_dir, vec3 base_color, vec3 specular_color,
                    float diffuse_weight, float roughness)
{
    if (u_ibl_params.x < 0.5)
    {
        return vec3_splat(0.0);
    }
    float n_dot_v = saturate(dot(normal, view_dir));
    vec3 reflection = reflect(-view_dir, normal);
    float mip = clamp(roughness * u_ibl_params.z, 0.0, u_ibl_params.z);
    // Diffuse map stores cosine-weighted mean radiance; no additional PI division.
    vec3 irradiance = textureCube(s_ibl_irradiance, normal).rgb;
    vec3 prefiltered = textureCubeLod(s_ibl_prefilter, reflection, mip).rgb;
    vec2 lut = texture2D(s_ibl_lut, vec2(n_dot_v, roughness)).rg;
    vec3 diffuse = base_color * diffuse_weight * irradiance;
    vec3 specular = prefiltered * (specular_color * lut.x + vec3_splat(lut.y));
    return (diffuse + specular) * u_ibl_params.y;
}

vec3 direct_light(int index, vec3 world_position, vec3 normal, vec3 view_dir, vec3 base_color,
                  float metallic, float roughness, vec3 specular_color, float smoothness)
{
    vec3 light_dir = -u_light_direction_outer[index].xyz;
    float attenuation = 1.0;
    float kind = u_light_kind_inner[index].x;
    if (kind > 0.5)
    {
        vec3 offset = u_light_position_range[index].xyz - world_position;
        float distance_squared = dot(offset, offset);
        float distance = sqrt(distance_squared);
        light_dir = offset / max(distance, 0.0001);
        float fade = saturate(1.0 - distance / u_light_position_range[index].w);
        attenuation = fade * fade / max(distance_squared, 0.01);
        if (kind > 1.5)
        {
            float angle = dot(-light_dir, u_light_direction_outer[index].xyz);
            attenuation *= smoothstep(u_light_direction_outer[index].w,
                                       u_light_kind_inner[index].y, angle);
        }
    }
    float n_dot_l = saturate(dot(normal, light_dir));
    float n_dot_v = max(dot(normal, view_dir), 0.0001);
    vec3 halfway_sum = view_dir + light_dir;
    vec3 halfway = halfway_sum / max(length(halfway_sum), 0.0001);
    float n_dot_h = saturate(dot(normal, halfway));
    float v_dot_h = saturate(dot(view_dir, halfway));
    vec3 response;
    if (u_material.z > 1.5)
    {
        // 18-ibl calcLambert/calcBlinn/specPwr adapted to an empirical surface response:
        // no normalization, Fresnel or N.L multiplier on the highlight; exponent bias is 1.
        float exponent = exp2(10.0 * smoothness + 1.0);
        vec3 highlight = specular_color * pow(n_dot_h, exponent) * u_specular_options.y;
        response = base_color * n_dot_l + highlight;
    }
    else
    {
        vec3 specular = pbr_specular(roughness, specular_color, n_dot_l, n_dot_v, n_dot_h, v_dot_h);
        specular *= u_specular_options.y;
        vec3 diffuse = (vec3_splat(1.0) - specular_color) * (1.0 - metallic) * base_color / 3.14159265;
        if (u_specular.w > 0.5)
        {
            float reflectivity = max(max(specular_color.r, specular_color.g), specular_color.b);
            diffuse = (1.0 - reflectivity) * base_color / 3.14159265;
        }
        response = (diffuse + specular) * n_dot_l;
    }
    vec3 radiance = u_light_color_intensity[index].rgb * u_light_color_intensity[index].w;
    float visibility = 1.0;
    if (u_shadow_settings.w > 0.5 && abs(float(index) - u_shadow_settings.z) < 0.5)
    {
        visibility = shadow_visibility(world_position);
    }
    return response * radiance * attenuation * visibility;
}

void main()
{
    // sRGB decode happens in the texture sampler; normal and ORM are linear data.
    vec2 uv = v_texcoord0 * u_uv_transform.xy + u_uv_transform.zw;
    vec4 base_sample = texture2D(s_base_color, uv) * u_base_color;
    vec4 base_color = base_sample * v_color0;
    apply_alpha_cutoff(base_color.a, u_material.w);
    vec3 normal = normalize(v_world_normal);
    vec3 tangent = normalize(v_world_tangent.xyz - normal * dot(normal, v_world_tangent.xyz));
    vec3 bitangent = cross(normal, tangent) * v_world_tangent.w;
    // Tangent-space sampling follows bgfx 06-bump; use full XYZ normal maps.
    vec3 sampled_normal = texture2D(s_normal, uv).xyz * 2.0 - 1.0;
    sampled_normal.xy *= u_surface_parameters.y;
    float normal_length = length(sampled_normal);
    sampled_normal = normal_length > 0.0001 ? sampled_normal / normal_length : vec3(0.0, 0.0, 1.0);
    normal = normalize(tangent * sampled_normal.x + bitangent * sampled_normal.y + normal * sampled_normal.z);
    vec3 orm = texture2D(s_orm, uv).rgb;
    float metallic = saturate(u_material.x * orm.b);
    float roughness = max(u_material.y * orm.g, 0.045);
    vec3 specular_color = mix(vec3_splat(0.04), base_color.rgb, metallic);
    float diffuse_weight = 1.0 - metallic;
    float smoothness = saturate(u_surface_parameters.x);
    if (u_specular.w > 0.5 || u_material.z > 1.5)
    {
        vec4 specular_sample = texture2D(s_specular, uv);
        specular_color = saturate(u_specular.rgb * specular_sample.rgb);
        smoothness = saturate(u_specular_options.x > 0.5 ? base_sample.a
                                                       : smoothness * specular_sample.a);
        roughness = max(1.0 - smoothness, 0.045);
        diffuse_weight = 1.0 - max(max(specular_color.r, specular_color.g), specular_color.b);
    }
    vec3 color = base_color.rgb;
    vec3 view_offset = u_camera_position.xyz - v_world_position;
    bool unlit = u_material.z > 0.5 && u_material.z < 1.5;
    if (!unlit)
    {
        vec3 view_dir = view_offset / max(length(view_offset), 0.0001);
        // Use the unperturbed normal to orient back-face lighting consistently across backends
        // and mirrored transforms, without flipping bumps on a front-facing surface.
        if (u_surface.x > 0.5 && dot(v_world_normal, view_dir) < 0.0)
        {
            normal = -normal;
        }
        vec3 ambient_color = u_ambient.rgb;
        if (u_ambient_sky.w > 0.5)
        {
            float height = clamp(normal.y, -1.0, 1.0);
            ambient_color = u_ambient_sky.rgb * max(height, 0.0)
                          + u_ambient_ground.rgb * max(-height, 0.0)
                          + u_ambient_equator.rgb * (1.0 - abs(height));
        }
        float ambient_weight = u_material.z > 1.5 ? 1.0 : diffuse_weight;
        color = base_color.rgb * ambient_weight * ambient_color * u_ambient.w;
        color += indirect_light(normal, view_dir, base_color.rgb, specular_color,
                                diffuse_weight, roughness);
        if (u_ambient_occlusion.z > 0.5)
        {
            vec2 screen_uv = gl_FragCoord.xy * u_ambient_occlusion.xy;
            color *= texture2D(s_ambient_occlusion, screen_uv).r;
        }
        for (int index = 0; index < 64; ++index)
        {
            if (float(index) < u_light_count.x)
            {
                color += direct_light(index, v_world_position, normal, view_dir, base_color.rgb,
                                      metallic, roughness, specular_color, smoothness);
            }
        }
        color *= orm.r;
    }
    color += texture2D(s_emission, uv).rgb * u_emission.rgb;
    if (u_fog_color.w > 0.5)
    {
        float fog = saturate((length(view_offset) - u_fog_range.x)
                             / max(u_fog_range.y - u_fog_range.x, 0.0001));
        if (u_fog_color.w > 1.5)
        {
            float depth = max(-mul(u_view, vec4(v_world_position, 1.0)).z - u_fog_range.w, 0.0);
            float optical_depth = min(u_fog_range.z * depth, 80.0);
            float exponent = u_fog_color.w > 2.5 ? optical_depth * optical_depth : optical_depth;
            // 16-shadowmaps transmittance, expressed as exp2(-x) to avoid overflow.
            fog = 1.0 - exp2(-exponent * 1.4426950408889634);
        }
        color = mix(color, u_fog_color.rgb, fog * u_surface_parameters.z);
    }
    if (u_tonemap.z > 0.5)
    {
        // The HDR resolve owns exposure, tone mapping and gamma for the complete scene.
        // Keep RGBA16F scene values finite, including bright emission and unlit materials.
        gl_FragColor = vec4(clamp(color, vec3_splat(0.0), vec3_splat(65504.0)), 1.0);
        return;
    }
    if (!unlit)
    {
        color *= u_tonemap.x;
        color = u_tonemap.y > 0.5 ? aces_filmic(color) : color / (vec3_splat(1.0) + color);
    }
    // Linear scene values to the current SDR, non-sRGB swapchain.
    color = pow(max(color, vec3_splat(0.0)), vec3_splat(1.0 / 2.2));
    gl_FragColor = vec4(color, 1.0);
}
