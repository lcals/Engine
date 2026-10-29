$input a_position, a_normal
$output v_world_position, v_world_normal

#include <bgfx_shader.sh>

uniform mat4 u_normal_transform;

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_world_position = mul(u_model[0], vec4(a_position, 1.0)).xyz;
    v_world_normal = mul(u_normal_transform, vec4(a_normal, 0.0)).xyz;
}
