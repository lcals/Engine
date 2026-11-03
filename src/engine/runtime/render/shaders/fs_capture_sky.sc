$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLERCUBE(s_ibl_prefilter, 13);
uniform mat4 u_capture_inverse;
uniform vec4 u_capture_mode;
uniform vec4 u_ibl_params;
uniform vec4 u_ambient;
uniform vec4 u_ambient_sky;
uniform vec4 u_ambient_equator;
uniform vec4 u_ambient_ground;
void main()
{
    vec2 xy = v_texcoord0 * 2.0 - 1.0;
    if (u_capture_mode.z < 0.5) { xy.y = -xy.y; }
    vec4 far_point = mul(u_capture_inverse, vec4(xy, 1.0, 1.0));
    vec4 near_point = mul(u_capture_inverse, vec4(xy, u_capture_mode.y, 1.0));
    vec3 ray = normalize(far_point.xyz / far_point.w - near_point.xyz / near_point.w);
    vec3 color = u_ambient.rgb;
    if (u_ambient_sky.w > 0.5)
    {
        color = u_ambient_sky.rgb * max(ray.y, 0.0)
              + u_ambient_ground.rgb * max(-ray.y, 0.0)
              + u_ambient_equator.rgb * (1.0 - abs(ray.y));
    }
    color *= u_ambient.w;
    if (u_ibl_params.x > 0.5)
    {
        color += textureCubeLod(s_ibl_prefilter, ray, 0.0).rgb * u_ibl_params.y;
    }
    gl_FragData[0] = vec4(color, 1.0);
    gl_FragData[1] = vec4_splat(0.0);
}
