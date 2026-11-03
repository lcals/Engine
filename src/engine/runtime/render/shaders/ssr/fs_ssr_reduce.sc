$input v_texcoord0
#include "bgfx_shader.sh"
#include "ssr_common.sh"
SAMPLER2D(s_ssr_depth, 2);

void main()
{
    if (u_ssr_reduce.w > 0.5)
    {
        gl_FragColor = vec4_splat(texture2D(s_ssr_depth, v_texcoord0).r);
        return;
    }
    vec2 destination = floor(v_texcoord0 * u_ssr_target.xy);
    vec2 first = floor(destination * u_ssr_reduce.xy / u_ssr_target.xy);
    vec2 end = ceil((destination + vec2_splat(1.0)) * u_ssr_reduce.xy / u_ssr_target.xy);
    float nearest_depth = 1.0;
    // Odd dimensions need a third source texel on an edge; omitting it loses occluders.
    for (int y = 0; y < 3; ++y)
    {
        for (int x = 0; x < 3; ++x)
        {
            vec2 pixel = first + vec2(float(x), float(y));
            if (all(lessThan(pixel, end)))
            {
                vec2 uv = (pixel + 0.5) / u_ssr_reduce.xy;
                nearest_depth = min(nearest_depth,
                    texture2DLod(s_ssr_depth, uv, u_ssr_reduce.z).r);
            }
        }
    }
    gl_FragColor = vec4_splat(nearest_depth);
}
