$input v_shadow_position, v_texcoord0, v_color0

#include <bgfx_shader.sh>
#include "shadow_helpers.sh"
#include "material_helpers.sh"

uniform vec4 u_shadow_depth;
// x: alpha cutoff, y: base color alpha.
uniform vec4 u_shadow_alpha;
uniform vec4 u_shadow_uv;
SAMPLER2D(s_shadow_alpha, 0);

void main()
{
    vec2 uv = v_texcoord0 * u_shadow_uv.xy + u_shadow_uv.zw;
    float alpha = texture2D(s_shadow_alpha, uv).a * u_shadow_alpha.y * v_color0.a;
    apply_alpha_cutoff(alpha, u_shadow_alpha.x);
    float depth = v_shadow_position.z / v_shadow_position.w;
    depth = depth * u_shadow_depth.x + u_shadow_depth.y;
    gl_FragColor = pack_shadow_depth(min(depth, 0.999999));
}
