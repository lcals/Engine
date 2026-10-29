$input a_position
$output v_shadow_position

// bgfx 15-shadowmaps-simple, BSD-2-Clause. See shadow_helpers.sh.
#include <bgfx_shader.sh>

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_shadow_position = gl_Position;
}
