$input v_texcoord0
#include "bgfx_shader.sh"
#include "ssr_common.sh"
SAMPLER2D(s_ssr_scene, 0);
SAMPLER2D(s_ssr_ibl, 1);
SAMPLER2D(s_ssr_depth, 2);
SAMPLER2D(s_ssr_normal, 3);
SAMPLER2D(s_ssr_specular, 4); // Trace result in this local pass layout.
SAMPLER2D(s_ssr_lut, 5); // Material validity in this local pass layout.

void main()
{
    vec4 scene = texture2D(s_ssr_scene, v_texcoord0);
    if (texture2D(s_ssr_lut, v_texcoord0).a < 0.5)
    {
        gl_FragColor = scene;
        return;
    }
    vec3 normal = texture2D(s_ssr_normal, v_texcoord0).xyz;
    float depth = texture2D(s_ssr_depth, v_texcoord0).r;
    float linear_depth = ssr_view_position(v_texcoord0, depth).z;
    vec2 location = v_texcoord0 * u_ssr_dimensions.zw - 0.5;
    vec2 base = floor(location);
    vec2 blend = fract(location);
    vec3 reflected = vec3_splat(0.0);
    float confidence = 0.0;
    float total = 0.0;
    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 2; ++x)
        {
            vec2 uv = (base + vec2(float(x), float(y)) + 0.5) / u_ssr_dimensions.zw;
            vec4 sample_value = texture2D(s_ssr_specular, uv);
            vec3 sample_normal = texture2D(s_ssr_normal, uv).xyz;
            float sample_depth = texture2D(s_ssr_depth, uv).r;
            float delta = abs(ssr_view_position(uv, sample_depth).z - linear_depth);
            float weight = (x == 0 ? 1.0 - blend.x : blend.x) * (y == 0 ? 1.0 - blend.y : blend.y);
            weight *= pow(saturate(dot(normal, sample_normal)), 16.0) *
                       exp2(-delta / max(u_ssr_parameters.z, 0.001));
            reflected += sample_value.rgb * sample_value.a * weight;
            confidence += sample_value.a * weight;
            total += weight;
        }
    }
    if (total > 0.0001)
    {
        vec3 old_specular = texture2D(s_ssr_ibl, v_texcoord0).rgb;
        scene.rgb += reflected / total - old_specular * (confidence / total);
    }
    gl_FragColor = vec4(clamp(scene.rgb, vec3_splat(0.0), vec3_splat(65504.0)), scene.a);
}
