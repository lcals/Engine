// Shared surface evaluation adapted from bgfx 06-bump / 18-ibl and Ant PBR.
// Licenses: third_party/notices/bgfx.txt and ant.txt.
#ifndef ENGINE_PBR_SURFACE_SH
#define ENGINE_PBR_SURFACE_SH
#include <bgfx_shader.sh>
#include "material_helpers.sh"
SAMPLER2D(s_base_color, 0);
SAMPLER2D(s_normal, 1);
SAMPLER2D(s_orm, 2);
SAMPLER2D(s_specular, 3);
SAMPLER2D(s_emission, 4);
uniform vec4 u_base_color;
// x: metallic, y: roughness, z: lighting (0 PBR, 1 unlit, 2 Blinn-Phong), w: alpha cutoff.
uniform vec4 u_material;
// x: back faces can be visible, y: blend mode (0 opaque, 1 alpha, 2 premultiplied).
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

struct PbrSurface
{
    vec4 base_color;
    vec3 normal;
    vec3 specular_color;
    vec3 emission;
    float metallic;
    float roughness;
    float diffuse_weight;
    float smoothness;
    float occlusion;
};

PbrSurface sample_pbr_surface(vec2 texcoord, vec4 vertex_color, vec3 world_normal,
                              vec4 world_tangent, vec3 view_direction)
{
    // sRGB decode happens in the texture sampler; normal and ORM are linear data.
    vec2 uv = texcoord * u_uv_transform.xy + u_uv_transform.zw;
    vec4 base_sample = texture2D(s_base_color, uv) * u_base_color;
    vec4 base_color = base_sample * vertex_color;
    apply_alpha_cutoff(base_color.a, u_material.w);
    vec3 normal = normalize(world_normal);
    vec3 tangent = normalize(world_tangent.xyz - normal * dot(normal, world_tangent.xyz));
    vec3 bitangent = cross(normal, tangent) * world_tangent.w;
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

    if (u_surface.x > 0.5 && dot(world_normal, view_direction) < 0.0)
    {
        normal = -normal;
    }
    PbrSurface result;
    result.base_color = base_color;
    result.normal = normal;
    result.specular_color = specular_color;
    result.emission = texture2D(s_emission, uv).rgb * u_emission.rgb;
    result.metallic = metallic;
    result.roughness = roughness;
    result.diffuse_weight = diffuse_weight;
    result.smoothness = smoothness;
    result.occlusion = orm.r;
    return result;
}
#endif
