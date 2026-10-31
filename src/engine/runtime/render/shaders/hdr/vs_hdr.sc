$input a_position, a_texcoord0
$output v_texcoord0

/*
 * Adapted from bgfx examples/09-hdr, Copyright 2011-2025 Branimir Karadzic.
 * BSD-2-Clause; see third_party/notices/bgfx-hdr.md.
 * The caller supplies clip-space positions and backend-correct texture coordinates.
 */

#include <bgfx_shader.sh>

void main()
{
    gl_Position = vec4(a_position, 1.0);
    v_texcoord0 = a_texcoord0;
}
