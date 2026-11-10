/*
 * Cascaded shadow sampling adapts bgfx examples/16-shadowmaps/
 * fs_shadowmaps_color_lighting_main.sh and 15-shadowmaps-simple/fs_sms_shadow.sh
 * at 7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099. BSD-2-Clause.
 * Copyright 2013-2014 Dario Manesku. See third_party/notices/bgfx.txt.
 * Changes: one atlas, camera-depth selection, overlapping split transitions,
 * distance fade and per-tap tile clamping. Packing stays in shadow_helpers.sh.
 */

SAMPLER2D(s_shadow_map, 15);

// World position to each cascade's local [0, 1] UV and depth.
uniform mat4 u_shadow_matrices[4];
// Atlas UV offset (xy) and tile scale (zw).
uniform vec4 u_shadow_rects[4];
// Positive camera-space far depth of each cascade.
uniform vec4 u_shadow_splits;
// Cascade count (1, 2, 4), blend fraction, camera near, shadow far.
uniform vec4 u_shadow_params;
// Depth bias, reciprocal atlas size, directional light index, enabled.
uniform vec4 u_shadow_settings;

// Receiver-plane depth gradients adapt Microsoft's CascadedShadowMaps11,
// CalculateRightAndUpTexelDepthDeltas. See third_party/notices/directx-shadow-bias.md.
float shadow_cascade_visibility(int cascade, vec3 world_position, vec3 world_dx, vec3 world_dy)
{
    // Static array indices also work on backends with restricted uniform indexing.
    mat4 projection;
    vec4 rectangle;
    if (cascade == 0)
    {
        projection = u_shadow_matrices[0];
        rectangle = u_shadow_rects[0];
    }
    else if (cascade == 1)
    {
        projection = u_shadow_matrices[1];
        rectangle = u_shadow_rects[1];
    }
    else if (cascade == 2)
    {
        projection = u_shadow_matrices[2];
        rectangle = u_shadow_rects[2];
    }
    else
    {
        projection = u_shadow_matrices[3];
        rectangle = u_shadow_rects[3];
    }
    vec4 coordinate = mul(projection, vec4(world_position, 1.0));
    if (!(coordinate.w > 0.0))
    {
        return 1.0;
    }
    vec3 projected = coordinate.xyz / coordinate.w;
    if (!all(greaterThanEqual(projected, vec3_splat(0.0))) ||
        !all(lessThanEqual(projected, vec3_splat(1.0))))
    {
        return 1.0;
    }

    vec3 dx = mul(projection, vec4(world_dx, 0.0)).xyz;
    vec3 dy = mul(projection, vec4(world_dy, 0.0)).xyz;
    float determinant = dx.x * dy.y - dx.y * dy.x;
    float determinant_scale = abs(dx.x * dy.y) + abs(dx.y * dy.x);
    vec2 depth_gradient = vec2_splat(0.0);
    if (abs(determinant) > determinant_scale * 0.000001)
    {
        depth_gradient = vec2(dy.y * dx.z - dx.y * dy.z,
                              dx.x * dy.z - dy.x * dx.z) / determinant;
    }
    vec2 uv = rectangle.xy + projected.xy * rectangle.zw;
    vec2 half_texel = vec2_splat(u_shadow_settings.y * 0.5);
    vec2 minimum_uv = rectangle.xy + half_texel;
    vec2 maximum_uv = rectangle.xy + rectangle.zw - half_texel;
    // Filter comparison results, not packed RGBA depth. The four bilinear PCF
    // taps per axis share five texels; combine their weights before fetching.
    // Keeping the fractional footprint avoids a snapped 4x4 stencil's stripes.
    vec2 footprint = uv / u_shadow_settings.y - 2.0;
    vec2 first_texel = floor(footprint);
    vec2 fraction = fract(footprint);
    float visible = 0.0;
    for (int x = 0; x < 5; ++x)
    {
        float weight_x = x == 0 ? 1.0 - fraction.x : (x == 4 ? fraction.x : 1.0);
        for (int y = 0; y < 5; ++y)
        {
            float weight_y = y == 0 ? 1.0 - fraction.y : (y == 4 ? fraction.y : 1.0);
            vec2 centre = (first_texel + vec2(float(x), float(y)) + 0.5) * u_shadow_settings.y;
            vec2 sample_uv = clamp(centre, minimum_uv, maximum_uv);
            // Packed-depth textures use POINT sampling. Compare against the receiver
            // plane at the actual texel centre, including the subtexel displacement.
            float depth = unpack_shadow_depth(texture2DLod(s_shadow_map, sample_uv, 0.0));
            float receiver_depth = projected.z +
                dot(depth_gradient, (sample_uv - uv) / rectangle.zw);
            visible += weight_x * weight_y * step(receiver_depth - u_shadow_settings.x, depth);
        }
    }
    return visible / 16.0;
}

float shadow_visibility(vec3 world_position, vec3 world_dx, vec3 world_dy)
{
    // The caller supplies screen derivatives taken before divergent light loops.
    if (u_shadow_settings.w < 0.5 || u_shadow_params.x < 0.5)
    {
        return 1.0;
    }
    float camera_depth = -mul(u_view, vec4(world_position, 1.0)).z;
    if (!(camera_depth >= u_shadow_params.z && camera_depth < u_shadow_params.w))
    {
        return 1.0;
    }

    int cascade = 0;
    float split_near = u_shadow_params.z;
    float split_far = u_shadow_splits.x;
    if (u_shadow_params.x > 1.5 && camera_depth > u_shadow_splits.x)
    {
        cascade = 1;
        split_near = u_shadow_splits.x;
        split_far = u_shadow_splits.y;
    }
    if (u_shadow_params.x > 2.5 && camera_depth > u_shadow_splits.y)
    {
        cascade = 2;
        split_near = u_shadow_splits.y;
        split_far = u_shadow_splits.z;
    }
    if (u_shadow_params.x > 2.5 && camera_depth > u_shadow_splits.z)
    {
        cascade = 3;
        split_near = u_shadow_splits.z;
        split_far = u_shadow_splits.w;
    }

    float visible = shadow_cascade_visibility(cascade, world_position, world_dx, world_dy);
    float blend_width = (split_far - split_near) * u_shadow_params.y;
    float blend_start = split_far - blend_width;
    if (blend_width > 0.0 && camera_depth > blend_start)
    {
        float next_visible = 1.0;
        if (float(cascade + 1) < u_shadow_params.x)
        {
            next_visible = shadow_cascade_visibility(cascade + 1, world_position, world_dx, world_dy);
        }
        // The final cascade blends with the unshadowed result beyond shadow far.
        float weight = smoothstep(blend_start, split_far, camera_depth);
        visible = mix(visible, next_visible, weight);
    }
    return visible;
}
