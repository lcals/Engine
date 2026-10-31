$input v_texcoord0

/*
 * Adapted from bgfx examples/09-hdr/fs_hdr_tonemap.sc and vs_hdr_tonemap.sc.
 * Copyright 2011-2025 Branimir Karadzic. BSD-2-Clause.
 * See third_party/notices/bgfx-hdr.md. Compose linear scene and Bloom before the
 * single exposure, tone mapping and gamma conversion; UI is drawn afterward.
 */

#include "hdr_helpers.sh"

SAMPLER2D(s_hdr_scene, 0);
SAMPLER2D(s_hdr_bloom, 1);

void main()
{
    vec3 color = texture2D(s_hdr_scene, v_texcoord0).rgb;
    if (u_hdr_params.z > 0.0)
    {
        color += hdr_blur9(s_hdr_bloom, v_texcoord0, vec2(u_hdr_texel.x, 0.0)) * u_hdr_params.z;
    }
    gl_FragColor = vec4(hdr_display_color(color), 1.0);
}
