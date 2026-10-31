$input v_texcoord0

/*
 * Adapted from bgfx examples/09-hdr/fs_hdr_bright.sc.
 * Copyright 2011-2025 Branimir Karadzic. BSD-2-Clause.
 * See third_party/notices/bgfx-hdr.md. Use a centered 3x3 source kernel;
 * threshold linear radiance before exposure, without intermediate tone mapping or gamma.
 */

#include "hdr_helpers.sh"

SAMPLER2D(s_hdr_scene, 0);

void main()
{
    vec2 step_size = u_hdr_texel.xy;
    vec3 color = texture2D(s_hdr_scene, v_texcoord0).rgb;
    color += texture2D(s_hdr_scene, v_texcoord0 + vec2(-step_size.x, -step_size.y)).rgb;
    color += texture2D(s_hdr_scene, v_texcoord0 + vec2(0.0, -step_size.y)).rgb;
    color += texture2D(s_hdr_scene, v_texcoord0 + vec2(step_size.x, -step_size.y)).rgb;
    color += texture2D(s_hdr_scene, v_texcoord0 + vec2(-step_size.x, 0.0)).rgb;
    color += texture2D(s_hdr_scene, v_texcoord0 + vec2(step_size.x, 0.0)).rgb;
    color += texture2D(s_hdr_scene, v_texcoord0 + vec2(-step_size.x, step_size.y)).rgb;
    color += texture2D(s_hdr_scene, v_texcoord0 + vec2(0.0, step_size.y)).rgb;
    color += texture2D(s_hdr_scene, v_texcoord0 + vec2(step_size.x, step_size.y)).rgb;
    color = max(color / 9.0 - vec3_splat(u_hdr_params.w), vec3_splat(0.0));
    gl_FragColor = vec4(color, 1.0);
}
