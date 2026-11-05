// Shared direct lighting for opaque PBR and water.
// bgfx and The Forge attribution is retained in fs_cube.sc and pbr_helpers.sh.
#ifndef ENGINE_PBR_LIGHTING_SH
#define ENGINE_PBR_LIGHTING_SH
vec3 direct_light(uint index, vec3 world_position, vec3 world_dx, vec3 world_dy,
                  vec3 normal, vec3 base_normal, vec3 view_dir, vec3 base_color,
                  float metallic, float roughness, vec3 specular_color, float smoothness)
{
    ClusterLightData light = load_cluster_light(index);
    vec3 light_dir = -light.direction_outer.xyz;
    float attenuation = 1.0;
    float kind = light.kind_inner.x;
    if (kind > 0.5)
    {
        vec3 offset = light.position_range.xyz - world_position;
        float distance_squared = dot(offset, offset);
        float distance = sqrt(distance_squared);
        light_dir = offset / max(distance, 0.0001);
        float fade = saturate(1.0 - distance / light.position_range.w);
        attenuation = fade * fade / max(distance_squared, 0.01);
        if (kind > 1.5)
        {
            float angle = dot(-light_dir, light.direction_outer.xyz);
            attenuation *= smoothstep(light.direction_outer.w,
                                       light.kind_inner.y, angle);
        }
    }
    // A normal map describes detail on the interpolated base surface; it must
    // not admit direct light through the opposite side of that surface. The
    // product also handles back-facing double-sided materials. This uses the
    // smooth base normal, not a per-triangle geometric normal or ray visibility.
    if (dot(base_normal, light_dir) * dot(base_normal, normal) <= 0.0)
    {
        return vec3_splat(0.0);
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
        if (u_capture_mode.x > 0.5)
        {
            float reflectivity = max(max(specular_color.r, specular_color.g), specular_color.b);
            response = (1.0 - reflectivity) * base_color * n_dot_l / 3.14159265;
        }
    }
    else
    {
        vec3 specular = pbr_specular(roughness, specular_color, n_dot_l, n_dot_v, n_dot_h,
                                     v_dot_h);
        specular *= u_specular_options.y;
        // glTF mixes complete dielectric and metallic BRDFs. The dielectric
        // diffuse Fresnel must not be replaced by the mixed metallic F0.
        float dielectric_fresnel = 0.04 + 0.96 * pow(1.0 - v_dot_h, 5.0);
        vec3 diffuse = (1.0 - dielectric_fresnel) * (1.0 - metallic) * base_color / 3.14159265;
        if (u_specular.w > 0.5)
        {
            // KHR_materials_pbrSpecularGlossiness defines c_diff using max(F0):
            // https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Archived/KHR_materials_pbrSpecularGlossiness
            // glTF 2.0 Appendix B subsequently applies its vector Fresnel mix.
            float reflectivity = max(max(specular_color.r, specular_color.g), specular_color.b);
            vec3 fresnel = pbr_fresnel(specular_color, v_dot_h);
            diffuse = (vec3_splat(1.0) - fresnel) * (1.0 - reflectivity)
                      * base_color / 3.14159265;
        }
        // Apply the published microfacet bump-shadowing model to the diffuse
        // lobe only. IBL and specular keep their own visibility.
        diffuse *= pbr_bump_shadowing(normalize(base_normal), normal, light_dir);
        response = (diffuse + (u_capture_mode.x > 0.5 ? vec3_splat(0.0) : specular)) * n_dot_l;
    }
    vec3 radiance = light.color_intensity.rgb * light.color_intensity.w;
    float visibility = 1.0;
    if (u_shadow_settings.w > 0.5 && abs(float(index) - u_shadow_settings.z) < 0.5)
    {
        visibility = shadow_visibility(world_position, world_dx, world_dy);
    }
    if (kind > 0.5)
    {
        visibility *= local_shadow_visibility(index, world_position);
    }
    return response * radiance * attenuation * visibility;
}

#endif
