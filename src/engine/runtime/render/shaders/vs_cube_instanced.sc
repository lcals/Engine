$input a_position, a_normal, a_tangent, a_texcoord0, a_color0, a_texcoord1, i_data0, i_data1, i_data2, i_data3, i_data4, i_data5, i_data6
$output v_world_position, v_world_normal, v_world_tangent, v_texcoord0, v_color0, v_lightmap_uv

#include <bgfx_shader.sh>



// Tangent frame follows bgfx examples/06-bump/vs_bump.sc (BSD-2-Clause).
// Copyright 2010-2025 Branimir Karadzic. See third_party/notices/bgfx.txt.
// Changes: float attributes, inverse-transpose normals and mirrored model handedness.
void main()
{
    mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
    mat4 normal_transform = mtxFromCols(i_data4, i_data5, i_data6, vec4(0.0, 0.0, 0.0, 1.0));
    gl_Position = mul(u_viewProj, mul(model, vec4(a_position, 1.0)));
    v_world_position = mul(model, vec4(a_position, 1.0)).xyz;
    v_world_normal = mul(normal_transform, vec4(a_normal, 0.0)).xyz;
    vec3 axis_x = mul(model, vec4(1.0, 0.0, 0.0, 0.0)).xyz;
    vec3 axis_y = mul(model, vec4(0.0, 1.0, 0.0, 0.0)).xyz;
    vec3 axis_z = mul(model, vec4(0.0, 0.0, 1.0, 0.0)).xyz;
    float handedness = sign(dot(cross(axis_x, axis_y), axis_z));
    v_world_tangent = vec4(mul(model, vec4(a_tangent.xyz, 0.0)).xyz,
                          a_tangent.w * handedness);
    v_texcoord0 = a_texcoord0;
    v_color0 = a_color0;
    v_lightmap_uv = a_texcoord1;
}
