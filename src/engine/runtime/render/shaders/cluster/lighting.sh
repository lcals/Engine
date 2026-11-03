/*
 * Cluster addressing and light buffers adapted from Ant at
 * f32fbd4811e90c2433bf159aa40fb8f8aceb8b58, cluster_shading.sh.
 * Copyright (c) 2017-2024 lingxigames.com, MIT.
 * See third_party/notices/ant-clustered-lighting.md.
 */
#ifndef ENGINE_CLUSTER_LIGHTING_SH
#define ENGINE_CLUSTER_LIGHTING_SH

#include "bgfx_compute.sh"

BUFFER_RO(b_light_data, vec4, 5);
BUFFER_RO(b_cluster_indices, uint, 7);

uniform vec4 u_cluster_grid;
uniform vec4 u_cluster_camera;
uniform vec4 u_cluster_counts;

struct ClusterLightData
{
    vec4 position_range;
    vec4 direction_outer;
    vec4 color_intensity;
    vec4 kind_inner;
};

ClusterLightData load_cluster_light(uint index)
{
    ClusterLightData light;
    int base = int(index) * 4;
    light.position_range = b_light_data[base];
    light.direction_outer = b_light_data[base + 1];
    light.color_intensity = b_light_data[base + 2];
    light.kind_inner = b_light_data[base + 3];
    return light;
}

void cluster_range(vec2 pixel, float view_depth, out uint offset, out uint count,
                   out bool overflow)
{
    vec2 xy = clamp(floor(pixel / u_cluster_grid.w), vec2_splat(0.0),
                    u_cluster_grid.xy - vec2_splat(1.0));
    float z = floor(log2(max(view_depth, u_cluster_camera.x) / u_cluster_camera.x) *
                     u_cluster_grid.z / log2(u_cluster_camera.y / u_cluster_camera.x));
    z = clamp(z, 0.0, u_cluster_grid.z - 1.0);
    uint cluster = uint(xy.x + u_cluster_grid.x * (xy.y + u_cluster_grid.y * z));
    uint base = cluster * (uint(u_cluster_counts.y) + 1u);
    count = b_cluster_indices[base];
    overflow = count == 0xffffffffu;
    // The caller filters directional lights in this loop; those are evaluated separately.
    count = overflow ? uint(u_cluster_counts.x) : count;
    offset = base + 1u;
}

uint cluster_light_index(uint offset, uint ordinal, bool overflow)
{
    return overflow ? ordinal : b_cluster_indices[offset + ordinal];
}

#endif
