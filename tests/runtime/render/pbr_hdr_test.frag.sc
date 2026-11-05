#include <bgfx_shader.sh>
#include "pbr_helpers.sh"

// Exercise the actual shared direct-light implementation with a controlled
// directional, point and spot lights, without requiring any production capture API.
uniform vec4 u_test_parameters; // roughness, metallic, intensity, angle radians
uniform vec4 u_test_base_color;
uniform vec4 u_test_light; // type, distance, range, spotlight cosine
uniform vec4 u_material;
uniform vec4 u_specular_options;
uniform vec4 u_capture_mode;
uniform vec4 u_specular;
uniform vec4 u_shadow_settings;
struct ClusterLightData
{
    vec4 position_range;
    vec4 direction_outer;
    vec4 color_intensity;
    vec4 kind_inner;
};
ClusterLightData load_cluster_light(uint index)
{
    ClusterLightData light;
    vec3 toward_light = vec3(sin(u_test_parameters.w), 0.0, cos(u_test_parameters.w));
    vec3 tangent = vec3(cos(u_test_parameters.w), 0.0, -sin(u_test_parameters.w));
    light.position_range = vec4(toward_light * u_test_light.y, u_test_light.z);
    vec3 axis = -toward_light * u_test_light.w + tangent *
                sqrt(max(0.0, 1.0 - u_test_light.w * u_test_light.w));
    light.direction_outer = vec4(axis, 0.6);
    light.color_intensity = vec4(1.0, 1.0, 1.0, u_test_parameters.z);
    light.kind_inner = vec4(u_test_light.x, 0.9, 0.0, 0.0);
    return light;
}
float shadow_visibility(vec3 position, vec3 world_dx, vec3 world_dy) { return 1.0; }
float local_shadow_visibility(uint index, vec3 position) { return 1.0; }
#include "pbr_lighting.sh"
void main()
{
    vec3 normal = vec3(0.0, 0.0, 1.0);
    vec3 base_color = u_test_base_color.rgb;
    vec3 f0 = mix(vec3_splat(0.04), base_color, u_test_parameters.y);
    vec3 color = direct_light(0u, vec3_splat(0.0), vec3_splat(0.0), vec3_splat(0.0),
                              normal, normal, normal,
                              base_color, u_test_parameters.y, u_test_parameters.x,
                              f0, 0.0);
    gl_FragColor = vec4(color, 1.0);
}
