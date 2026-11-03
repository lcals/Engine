/*
 * Packed-depth PCF and projected local lights adapt bgfx 15-shadowmaps-simple
 * and 16-shadowmaps at 7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099.
 * Copyright 2013-2014 Dario Manesku, BSD-2-Clause; see notices/bgfx.txt.
 * Point lights use six ordinary 90-degree faces in the existing 2D atlas;
 * this is not the example's optional tetrahedral/stencil packing.
 */
#ifndef ENGINE_LOCAL_SHADOW_HELPERS_SH
#define ENGINE_LOCAL_SHADOW_HELPERS_SH

// Include cluster/lighting.sh, shadow_helpers.sh and csm_helpers.sh before this file.

vec4 local_shadow_project(uint record, vec3 position)
{
    int base = int(u_cluster_counts.z) + int(record) * 6;
    // Matrices are uploaded as four columns; this avoids backend constructor layout assumptions.
    return b_light_data[base] * position.x + b_light_data[base + 1] * position.y +
           b_light_data[base + 2] * position.z + b_light_data[base + 3];
}

uint point_shadow_face(vec3 direction)
{
    vec3 axis = abs(direction);
    if (axis.x >= axis.y && axis.x >= axis.z) return direction.x >= 0.0 ? 0u : 1u;
    if (axis.y >= axis.z) return direction.y >= 0.0 ? 2u : 3u;
    return direction.z >= 0.0 ? 4u : 5u;
}

vec3 point_shadow_ray(uint face, vec2 uv)
{
    vec3 forward = vec3(1.0, 0.0, 0.0);
    vec3 up = vec3(0.0, -1.0, 0.0);
    if (face == 1u) forward = vec3(-1.0, 0.0, 0.0);
    if (face == 2u) { forward = vec3(0.0, 1.0, 0.0); up = vec3(0.0, 0.0, 1.0); }
    if (face == 3u) { forward = vec3(0.0, -1.0, 0.0); up = vec3(0.0, 0.0, -1.0); }
    if (face == 4u) forward = vec3(0.0, 0.0, 1.0);
    if (face == 5u) forward = vec3(0.0, 0.0, -1.0);
    vec2 ndc = uv * 2.0 - 1.0;
    if (u_cluster_camera.w < 0.5) ndc.y = -ndc.y;
    vec3 right = cross(forward, up);
    return normalize(forward + right * ndc.x + up * ndc.y);
}

float local_shadow_visibility(uint light_index, vec3 world_position)
{
    ClusterLightData light = load_cluster_light(light_index);
    if (light.kind_inner.z < 0.5) return 1.0;
    uint first = uint(light.kind_inner.z) - 1u;
    bool is_point = light.kind_inner.w > 1.5;
    vec3 direction = world_position - light.position_range.xyz;
    float radial_distance = length(direction);
    uint face = is_point ? point_shadow_face(direction) : 0u;
    uint record = first + face;
    vec4 position = local_shadow_project(record, world_position);
    if (position.w <= 0.0) return 1.0;
    vec3 projected = position.xyz / position.w;
    if (any(lessThan(projected, vec3_splat(0.0))) ||
        any(greaterThan(projected, vec3_splat(1.0)))) return 1.0;
    int base = int(u_cluster_counts.z) + int(record) * 6;
    vec4 rectangle = b_light_data[base + 4];
    vec4 parameters = b_light_data[base + 5];
    float visible = 0.0;
    for (int x = 0; x < 4; ++x)
    {
        for (int y = 0; y < 4; ++y)
        {
            vec2 local_uv = projected.xy + (vec2(float(x), float(y)) - 1.5) *
                                            parameters.y / rectangle.zw;
            vec4 sample_rectangle = rectangle;
            float receiver_depth = projected.z;
            if (is_point)
            {
                // Reproject taps across cube edges into the adjacent face. Reconstruct
                // a receiver at the same radius, since each face stores perspective depth.
                vec3 ray = point_shadow_ray(face, local_uv);
                uint sample_face = point_shadow_face(ray);
                vec3 receiver = light.position_range.xyz + ray * radial_distance;
                vec4 p = local_shadow_project(first + sample_face, receiver);
                local_uv = p.xy / p.w;
                receiver_depth = p.z / p.w;
                int sample_base = int(u_cluster_counts.z) + int(first + sample_face) * 6;
                sample_rectangle = b_light_data[sample_base + 4];
            }
            vec2 uv = sample_rectangle.xy + local_uv * sample_rectangle.zw;
            vec2 half_texel = vec2_splat(parameters.y * 0.5);
            uv = clamp(uv, sample_rectangle.xy + half_texel,
                        sample_rectangle.xy + sample_rectangle.zw - half_texel);
            float sample_depth = unpack_shadow_depth(texture2D(s_shadow_map, uv));
            visible += step(receiver_depth - parameters.x, sample_depth);
        }
    }
    return visible / 16.0;
}

#endif
