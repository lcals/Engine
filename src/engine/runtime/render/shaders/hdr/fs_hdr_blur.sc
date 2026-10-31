$input v_texcoord0

/*
 * Adapted from bgfx examples/09-hdr/fs_hdr_blur.sc and vs_hdr_blur.sc.
 * Copyright 2011-2025 Branimir Karadzic. BSD-2-Clause.
 * See third_party/notices/bgfx-hdr.md.
 */

#include "hdr_helpers.sh"

SAMPLER2D(s_hdr_scene, 0);

void main()
{
    gl_FragColor = vec4(hdr_blur9(s_hdr_scene, v_texcoord0, vec2(0.0, u_hdr_texel.y)), 1.0);
}
