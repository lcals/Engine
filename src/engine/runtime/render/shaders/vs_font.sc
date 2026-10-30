$input a_position, a_color0, a_texcoord0, a_texcoord1, a_texcoord2
$output v_font_coord, v_font_range, v_color0

#include <bgfx_shader.sh>

// Dynamic pixel dilation from Eric Lengyel's public Slug reference (MIT).
// The full Copyright (c) 2017 Eric Lengyel license is retained in fs_font.sc.
// https://github.com/bkaradzic/bgfx/blob/a7016487e5970c5299bb837f1af42dcb24909a67/examples/51-gpufont/vs_slug.sc

void main()
{
    vec3 position = a_position;
    v_font_coord = a_texcoord0;
    vec2 outward = a_texcoord2.xy;
    if (dot(outward, outward) > 0.0)
    {
        vec2 direction = normalize(outward);
        vec4 p_clip = mul(u_modelViewProj, vec4(position, 1.0));
        vec4 n_clip = mul(u_modelViewProj, vec4(direction, 0.0, 0.0));
        float s = p_clip.w;
        float t = n_clip.w;
        vec2 screen_direction = (s * n_clip.xy - t * p_clip.xy) * u_viewRect.zw;
        float s2 = s * s;
        float st = s * t;
        float magnitude = dot(screen_direction, screen_direction);
        float denominator = magnitude - st * st;
        if (s > 0.0 && denominator > 0.0000001)
        {
            vec2 delta = outward * (s2 * (st + sqrt(magnitude)) / denominator);
            position.xy += delta;
            v_font_coord += delta * a_texcoord2.zw;
        }
    }
    gl_Position = mul(u_modelViewProj, vec4(position, 1.0));
    v_font_range = a_texcoord1;
    v_color0 = a_color0;
}
