$input v_shadow_position

#include <bgfx_shader.sh>
#include "shadow_helpers.sh"

uniform vec4 u_shadow_depth;

void main()
{
    float depth = v_shadow_position.z / v_shadow_position.w;
    depth = depth * u_shadow_depth.x + u_shadow_depth.y;
    gl_FragColor = pack_shadow_depth(min(depth, 0.999999));
}
