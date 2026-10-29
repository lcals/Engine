$input v_world_position, v_world_normal

#include <bgfx_shader.sh>

uniform vec4 u_base_color;
// x: metallic, y: roughness, z: unlit, w: unused.
uniform vec4 u_material;
uniform vec4 u_camera_position;
uniform vec4 u_light_direction;
uniform vec4 u_light_color;

// Direct-light Cook-Torrance BRDF: GGX distribution, correlated Smith visibility,
// Schlick Fresnel. Material and lighting values are linear; output is display encoded.
void main()
{
    vec3 color = u_base_color.rgb;
    if (u_material.z < 0.5)
    {
        vec3 normal = normalize(v_world_normal);
        vec3 view_dir = normalize(u_camera_position.xyz - v_world_position);
        vec3 light_dir = -u_light_direction.xyz;
        vec3 halfway_sum = view_dir + light_dir;
        vec3 halfway = halfway_sum / max(length(halfway_sum), 0.0001);
        float n_dot_l = max(dot(normal, light_dir), 0.0);
        float n_dot_v = max(dot(normal, view_dir), 0.0001);
        float n_dot_h = max(dot(normal, halfway), 0.0);
        float v_dot_h = max(dot(view_dir, halfway), 0.0);

        float metallic = u_material.x;
        float roughness = max(u_material.y, 0.045);
        float alpha = roughness * roughness;
        float alpha_squared = alpha * alpha;
        float distribution_denominator = n_dot_h * n_dot_h * (alpha_squared - 1.0) + 1.0;
        float distribution = alpha_squared /
            max(3.14159265 * distribution_denominator * distribution_denominator, 0.000001);
        float visibility_v = n_dot_l * sqrt(n_dot_v * n_dot_v * (1.0 - alpha_squared) + alpha_squared);
        float visibility_l = n_dot_v * sqrt(n_dot_l * n_dot_l * (1.0 - alpha_squared) + alpha_squared);
        float visibility = 0.5 / max(visibility_v + visibility_l, 0.00001);
        vec3 f0 = mix(vec3(0.04, 0.04, 0.04), u_base_color.rgb, metallic);
        float fresnel_factor = pow(1.0 - v_dot_h, 5.0);
        vec3 fresnel = f0 + (vec3(1.0, 1.0, 1.0) - f0) * fresnel_factor;
        vec3 specular = distribution * visibility * fresnel;
        vec3 diffuse = (vec3(1.0, 1.0, 1.0) - fresnel) * (1.0 - metallic) *
            u_base_color.rgb / 3.14159265;
        color = (diffuse + specular) * u_light_color.rgb * n_dot_l;
        // Reinhard tone mapping of the linear HDR lighting result.
        color = color / (vec3(1.0, 1.0, 1.0) + color);
    }
    color = pow(max(color, vec3(0.0, 0.0, 0.0)), vec3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
    gl_FragColor = vec4(color, 1.0);
}
