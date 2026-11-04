// Adapted from Ant (MIT), Copyright (c) 2017-2024 lingxigames.com.
// See third_party/notices/ant-scene-bake.md for upstream attribution and changes.
// Adaptive texel footprint sampling follows Filament's CubemapUtils (Apache-2.0).
// See third_party/notices/filament-panorama.md for the pinned source and license.
#include "ibl_common.sh"
SAMPLER2D(s_source, 0);
IMAGE2D_WO(s_result, rgba16f, 1);

vec2 panorama_uv(vec3 direction)
{
    float radius2 = direction.x * direction.x + direction.z * direction.z;
    float longitude = radius2 > 1e-16 ? atan2(direction.z, direction.x) : 0.0;
    return vec2(0.5 + 0.5 * longitude / M_PI,
                acos(clamp(direction.y, -1.0, 1.0)) / M_PI);
}

// The longitude interval wraps at the panorama seam. Unwrap each corner
// relative to the center so a texel straddling the seam remains a small region.
vec2 relative_uv(vec2 uv, vec2 center)
{
    float longitude = fract(uv.x - center.x + 0.5) - 0.5;
    return vec2(longitude, uv.y - center.y);
}

NUM_THREADS(WORKGROUP_THREADS, WORKGROUP_THREADS, 1)
void main()
{
    ivec2 size = imageSize(s_result);
    if (any(gl_GlobalInvocationID.xy >= size)) return;
    vec2 id = vec2(gl_GlobalInvocationID.xy);
    vec2 dimensions = vec2(size);
    vec2 center = panorama_uv(bake_direction_at(id + 0.5, dimensions));
    vec2 lo = vec2_splat(0.0);
    vec2 hi = vec2_splat(0.0);
    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 2; ++x)
        {
            vec3 corner_direction = bake_direction_at(id + vec2(float(x), float(y)), dimensions);
            vec2 corner = panorama_uv(corner_direction);
            // At a pole, longitude is undefined: it must not inflate or
            // arbitrarily rotate the estimated footprint.
            if (corner_direction.x * corner_direction.x +
                    corner_direction.z * corner_direction.z < 1e-16)
            {
                corner.x = center.x;
            }
            vec2 delta = relative_uv(corner, center);
            lo = min(lo, delta);
            hi = max(hi, delta);
        }
    }
    vec2 footprint = max((hi - lo) * vec2(textureSize(s_source, 0)), vec2_splat(1.0));
    // Finite adaptive sampling approximates an area filter, not exact integration.
    // Cap very large or polar footprints to bound each compute invocation.
    int count = int(clamp(ceil(footprint.x * footprint.y), 1.0, 1024.0));
    vec3 color = vec3_splat(0.0);
    float total = 0.0;
    for (int i = 0; i < count; ++i)
    {
        vec2 offset = count == 1 ? vec2_splat(0.5) :
            vec2((float(i) + 0.5) / float(count),
                 fract(radicalInverse_VdC(uint(i)) + 0.5 / float(count)));
        vec2 position = id + offset;
        vec2 face_uv = 2.0 * position / dimensions - 1.0;
        float weight = inversesqrt(1.0 + dot(face_uv, face_uv));
        weight = weight * weight * weight;
        vec2 uv = panorama_uv(bake_direction_at(position, dimensions));
        color += texture2DLod(s_source, uv, 0.0).rgb * weight;
        total += weight;
    }
    imageStore(s_result, ivec2(gl_GlobalInvocationID.xy), vec4(color / total, 1.0));
}
