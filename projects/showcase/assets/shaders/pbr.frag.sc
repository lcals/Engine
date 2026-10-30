$input v_world_position, v_world_normal, v_world_tangent, v_texcoord0, v_color0

#include <bgfx_shader.sh>
#include "pbr_helpers.sh"

SAMPLER2D(s_base_color, 0);
SAMPLER2D(s_normal, 1);
SAMPLER2D(s_orm, 2);
uniform vec4 u_base_color;
// x: metallic, y: roughness, z: unlit, w: unused.
uniform vec4 u_material;
uniform vec4 u_camera_position;
uniform vec4 u_light_count;
uniform vec4 u_light_position_range[64];
uniform vec4 u_light_direction_outer[64];
uniform vec4 u_light_color_intensity[64];
uniform vec4 u_light_kind_inner[64];

vec3 direct_light(int index, vec3 world_position, vec3 normal, vec3 view_dir, vec3 base_color,
                  float metallic, float roughness)
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
    vec3 specular_color = mix(vec3_splat(0.04), base_color, metallic);
    vec3 specular = pbr_specular(roughness, specular_color, n_dot_l, n_dot_v, n_dot_h, v_dot_h);
    vec3 diffuse = (vec3_splat(1.0) - specular_color) * (1.0 - metallic) * base_color / 3.14159265;
    vec3 radiance = u_light_color_intensity[index].rgb * u_light_color_intensity[index].w;
    return (diffuse + specular) * radiance * n_dot_l * attenuation;
}

void main()
{
    // sRGB decode happens in the texture sampler; normal and ORM are linear data.
    vec4 base_color = texture2D(s_base_color, v_texcoord0) * u_base_color * v_color0;
    vec3 normal = normalize(v_world_normal);
    vec3 tangent = normalize(v_world_tangent.xyz - normal * dot(normal, v_world_tangent.xyz));
    vec3 bitangent = cross(normal, tangent) * v_world_tangent.w;
    // Tangent-space sampling follows bgfx 06-bump; use full XYZ normal maps.
    vec3 sampled_normal = texture2D(s_normal, v_texcoord0).xyz * 2.0 - 1.0;
    float normal_length = length(sampled_normal);
    sampled_normal = normal_length > 0.0001 ? sampled_normal / normal_length : vec3(0.0, 0.0, 1.0);
    normal = normalize(tangent * sampled_normal.x + bitangent * sampled_normal.y + normal * sampled_normal.z);
    vec3 orm = texture2D(s_orm, v_texcoord0).rgb;
    float metallic = saturate(u_material.x * orm.b);
    float roughness = max(u_material.y * orm.g, 0.045);
    vec3 color = base_color.rgb;
    if (u_material.z < 0.5)
    {
        vec3 view_offset = u_camera_position.xyz - v_world_position;
        vec3 view_dir = view_offset / max(length(view_offset), 0.0001);
        color = vec3_splat(0.0);
        for (int index = 0; index < 64; ++index)
        {
            if (float(index) < u_light_count.x)
            {
                color += direct_light(index, v_world_position, normal, view_dir, base_color.rgb, metallic, roughness);
            }
        }
        color *= orm.r;
        color = color / (vec3_splat(1.0) + color);
    }
    // Linear scene values to the current SDR, non-sRGB swapchain.
    color = pow(max(color, vec3_splat(0.0)), vec3_splat(1.0 / 2.2));
    gl_FragColor = vec4(color, 1.0);
}
