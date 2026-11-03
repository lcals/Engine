/*
 * Full-frustum clustered light culling adapted from Ant cs_cluster_aabb.sc and
 * cs_lightcull.sc at f32fbd4811e90c2433bf159aa40fb8f8aceb8b58.
 * Copyright (c) 2017-2024 lingxigames.com, MIT.
 * See third_party/notices/ant-clustered-lighting.md.
 * Changes: fused bounds/culling, right-handed view, arbitrary viewport size,
 * conservative spot bounds, empty-list clearing and lossless overflow fallback.
 */
#include "bgfx_shader.sh"
#include "bgfx_compute.sh"

BUFFER_RO(b_cluster_lights, vec4, 0);
BUFFER_RW(b_cluster_output, uint, 1);

uniform vec4 u_cluster_grid;
uniform vec4 u_cluster_camera;
uniform vec4 u_cluster_counts;
uniform vec4 u_cluster_viewport;
uniform mat4 u_cluster_view;
uniform mat4 u_cluster_inverse_projection;

vec3 cluster_corner(vec2 pixel, float depth)
{
    vec2 xy = pixel * u_cluster_viewport.zw * 2.0 - 1.0;
    if (u_cluster_camera.w < 0.5) xy.y = -xy.y;
    float near_ndc = u_cluster_camera.z > 0.5 ? -1.0 : 0.0;
    vec4 near_point = mul(u_cluster_inverse_projection, vec4(xy, near_ndc, 1.0));
    // A point halfway through clip depth defines the same ray without a singular
    // far-plane w for an oblique reflection camera.
    vec4 far_point = mul(u_cluster_inverse_projection, vec4(xy, (near_ndc + 1.0) * 0.5, 1.0));
    vec3 p = near_point.xyz / near_point.w;
    vec3 q = far_point.xyz / far_point.w;
    return p + (q - p) * ((-depth - p.z) / (q.z - p.z));
}

NUM_THREADS(64, 1, 1)
void main()
{
    uint cluster = gl_GlobalInvocationID.x;
    if (cluster >= uint(u_cluster_counts.w)) return;
    uint nx = uint(u_cluster_grid.x);
    uint ny = uint(u_cluster_grid.y);
    uint x = cluster % nx;
    uint y = (cluster / nx) % ny;
    uint z = cluster / (nx * ny);
    float near_depth = u_cluster_camera.x *
        pow(u_cluster_camera.y / u_cluster_camera.x, float(z) / u_cluster_grid.z);
    float far_depth = u_cluster_camera.x *
        pow(u_cluster_camera.y / u_cluster_camera.x, float(z + 1u) / u_cluster_grid.z);
    vec2 first = vec2(float(x), float(y)) * u_cluster_grid.w;
    vec2 last = min(first + vec2_splat(u_cluster_grid.w), u_cluster_viewport.xy);
    vec3 minimum = vec3_splat(1e30);
    vec3 maximum = vec3_splat(-1e30);
    for (int corner = 0; corner < 8; ++corner)
    {
        vec2 pixel = vec2((corner & 1) == 0 ? first.x : last.x,
                          (corner & 2) == 0 ? first.y : last.y);
        vec3 corner_position = cluster_corner(pixel, (corner & 4) == 0 ? near_depth : far_depth);
        minimum = min(minimum, corner_position);
        maximum = max(maximum, corner_position);
    }
    uint capacity = uint(u_cluster_counts.y);
    uint base = cluster * (capacity + 1u);
    uint count = 0u;
    for (uint index = 0u; index < uint(u_cluster_counts.x); ++index)
    {
        int light_base = int(index) * 4;
        vec4 kind = b_cluster_lights[light_base + 3];
        if (kind.x < 0.5) continue;
        vec4 position_range = b_cluster_lights[light_base];
        vec3 position = mul(u_cluster_view, vec4(position_range.xyz, 1.0)).xyz;
        vec3 closest = clamp(position, minimum, maximum);
        vec3 distance = position - closest;
        // A spot cone is conservatively enclosed by its range sphere. The fragment
        // evaluates the exact cone, so no camera rotation or cone edge loses light.
        if (dot(distance, distance) > position_range.w * position_range.w) continue;
        if (count == capacity)
        {
            b_cluster_output[base] = 0xffffffffu;
            return;
        }
        b_cluster_output[base + 1u + count] = index;
        ++count;
    }
    b_cluster_output[base] = count;
}
