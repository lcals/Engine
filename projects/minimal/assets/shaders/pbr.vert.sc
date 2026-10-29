$input a_position, a_normal, a_tangent, a_texcoord0, a_color0
$output v_world_position, v_world_normal, v_world_tangent, v_texcoord0, v_color0

#include <bgfx_shader.sh>

uniform mat4 u_normal_transform;

// Tangent frame follows bgfx examples/06-bump/vs_bump.sc (BSD-2-Clause).
// Copyright 2010-2025 Branimir Karadzic. See third_party/notices/bgfx.txt.
// Changes: float attributes, inverse-transpose normals and mirrored model handedness.
void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_world_position = mul(u_model[0], vec4(a_position, 1.0)).xyz;
    v_world_normal = mul(u_normal_transform, vec4(a_normal, 0.0)).xyz;
    vec3 axis_x = mul(u_model[0], vec4(1.0, 0.0, 0.0, 0.0)).xyz;
    vec3 axis_y = mul(u_model[0], vec4(0.0, 1.0, 0.0, 0.0)).xyz;
    vec3 axis_z = mul(u_model[0], vec4(0.0, 0.0, 1.0, 0.0)).xyz;
    float handedness = sign(dot(cross(axis_x, axis_y), axis_z));
    v_world_tangent = vec4(mul(u_model[0], vec4(a_tangent.xyz, 0.0)).xyz,
                          a_tangent.w * handedness);
    v_texcoord0 = a_texcoord0;
    v_color0 = a_color0;
}
