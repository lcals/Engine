// bgfx 24-nbody compute-buffer dispatch; Ant cs_skinning.sc buffer/palette organization.
// bgfx: BSD-2-Clause, third_party/notices/bgfx.txt.
// Ant: Copyright (c) 2017-2024 lingxigames.com, MIT, third_party/notices/ant.txt.
// Engine adaptation: fixed 64-byte GpuVertex, independent inverse-transpose normals,
// tangent-frame handedness and per-instance output reused by color/AO/shadow passes.
#include <bgfx_compute.sh>

BUFFER_RO(b_skinning_source, vec4, 0);
BUFFER_RO(b_skinning_influences, vec4, 1);
BUFFER_RO(b_skinning_palette, vec4, 2);
BUFFER_WO(b_skinning_output, vec4, 3);

uniform vec4 u_skinning_parameters;

mat4 skinning_matrix(uint index)
{
    uint offset = index * 4u;
    return mtxFromCols(b_skinning_palette[offset], b_skinning_palette[offset + 1u],
                       b_skinning_palette[offset + 2u], b_skinning_palette[offset + 3u]);
}

vec3 skinning_normalize(vec3 value, vec3 fallback)
{
    float scale = max(max(abs(value.x), abs(value.y)), abs(value.z));
    return scale > 0.0 ? normalize(value / scale) : fallback;
}

NUM_THREADS(64, 1, 1)
void main()
{
    uint vertex_count = uint(u_skinning_parameters.x) + (uint(u_skinning_parameters.y) << 16u);
    uint joint_count = uint(u_skinning_parameters.z);
    uint vertex = gl_GlobalInvocationID.x +
                  gl_GlobalInvocationID.y * uint(u_skinning_parameters.w);
    if (vertex >= vertex_count)
    {
        return;
    }
    uint offset = vertex * 4u;
    vec4 source0 = b_skinning_source[offset];
    vec4 source1 = b_skinning_source[offset + 1u];
    vec4 source2 = b_skinning_source[offset + 2u];
    vec4 source3 = b_skinning_source[offset + 3u];
    vec3 position = source0.xyz;
    vec3 normal = vec3(source0.w, source1.x, source1.y);
    vec3 tangent = vec3(source1.z, source1.w, source2.x);
    vec3 bitangent = cross(normal, tangent) * source2.y;
    uvec4 joints = uvec4(b_skinning_influences[vertex * 2u]);
    vec4 weights = b_skinning_influences[vertex * 2u + 1u];
    mat4 transform = skinning_matrix(joints.x) * weights.x +
                     skinning_matrix(joints.y) * weights.y +
                     skinning_matrix(joints.z) * weights.z +
                     skinning_matrix(joints.w) * weights.w;
    mat4 normal_transform = skinning_matrix(joints.x + joint_count) * weights.x +
                            skinning_matrix(joints.y + joint_count) * weights.y +
                            skinning_matrix(joints.z + joint_count) * weights.z +
                            skinning_matrix(joints.w + joint_count) * weights.w;
    position = mul(transform, vec4(position, 1.0)).xyz;
    normal = skinning_normalize(mul(normal_transform, vec4(normal, 0.0)).xyz, normal);
    tangent = mul(transform, vec4(tangent, 0.0)).xyz;
    tangent -= normal * dot(normal, tangent);
    vec3 axis = abs(normal.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    tangent = skinning_normalize(tangent, normalize(cross(axis, normal)));
    bitangent = mul(transform, vec4(bitangent, 0.0)).xyz;
    float handedness = dot(cross(normal, tangent), bitangent) < 0.0 ? -1.0 : 1.0;
    b_skinning_output[offset] = vec4(position, normal.x);
    b_skinning_output[offset + 1u] = vec4(normal.yz, tangent.xy);
    b_skinning_output[offset + 2u] = vec4(tangent.z, handedness, source2.zw);
    b_skinning_output[offset + 3u] = source3;
}
