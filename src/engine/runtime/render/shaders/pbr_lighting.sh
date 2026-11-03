// Shared direct lighting for opaque PBR and water.
// bgfx and The Forge attribution is retained in fs_cube.sc and pbr_helpers.sh.
#ifndef ENGINE_PBR_LIGHTING_SH
#define ENGINE_PBR_LIGHTING_SH
vec3 direct_light(uint index, vec3 world_position, vec3 normal, vec3 view_dir, vec3 base_color,
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
        vec3 specular = pbr_specular(roughness, specular_color, n_dot_l, n_dot_v, n_dot_h, v_dot_h);
        specular *= u_specular_options.y;
        vec3 diffuse = (vec3_splat(1.0) - specular_color) * (1.0 - metallic) * base_color / 3.14159265;
        if (u_specular.w > 0.5)
        {
            float reflectivity = max(max(specular_color.r, specular_color.g), specular_color.b);
            diffuse = (1.0 - reflectivity) * base_color / 3.14159265;
        }
        response = (diffuse + (u_capture_mode.x > 0.5 ? vec3_splat(0.0) : specular)) * n_dot_l;
    }
    vec3 radiance = light.color_intensity.rgb * light.color_intensity.w;
    float visibility = 1.0;
    if (u_shadow_settings.w > 0.5 && abs(float(index) - u_shadow_settings.z) < 0.5)
    {
        visibility = shadow_visibility(world_position);
    }
    if (kind > 0.5)
    {
        visibility *= local_shadow_visibility(index, world_position);
    }
    return response * radiance * attenuation * visibility;
}

#endif
