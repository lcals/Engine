// Cubemap texel solid-angle weighting follows the area-element formulation
// used by lighting_bake_job.cpp (cubemap solid-angle integration).
#include "ibl_common.sh"

SAMPLERCUBE(s_source, 0);
IMAGE2D_WO(s_result, rgba16f, 1);

float area_element(float x, float y)
{
    return atan2(x * y, sqrt(x * x + y * y + 1.0));
}

float solid_angle(vec2 lo, vec2 hi)
{
    return area_element(hi.x, hi.y) - area_element(lo.x, hi.y) -
           area_element(hi.x, lo.y) + area_element(lo.x, lo.y);
}

NUM_THREADS(WORKGROUP_THREADS, WORKGROUP_THREADS, 1)
void main()
{
    ivec2 size = imageSize(s_result);
    ivec2 id = ivec2(gl_GlobalInvocationID.xy);
    if (any(id >= size)) return;

    if (u_bake_face.y == 0.0)
    {
        // Copy a single-level cube input into the independently owned cube.
        vec3 color = textureCubeLod(s_source, bake_direction(id, size), 0.0).rgb;
        imageStore(s_result, id, vec4(color, 1.0));
        return;
    }

    int previous_size = int(u_bake_face.z);
    ivec2 first = id * previous_size / size;
    // Odd-sized mip levels do not divide evenly into the next level. Include
    // every overlapping source texel and weight only the intersection;
    // assigning the shared texel wholly to one side shifts energy.
    ivec2 last = ((id + 1) * previous_size + size - 1) / size;
    vec2 destination_lo = 2.0 * vec2(id) / vec2(size) - 1.0;
    vec2 destination_hi = 2.0 * (vec2(id) + 1.0) / vec2(size) - 1.0;
    vec3 sum = vec3_splat(0.0);
    float weight_sum = 0.0;
    for (int y = 0; y < 3; ++y)
    {
        for (int x = 0; x < 3; ++x)
        {
            ivec2 pixel = first + ivec2(x, y);
            if (any(pixel >= last)) continue;
            vec2 source_lo = 2.0 * vec2(pixel) / float(previous_size) - 1.0;
            vec2 source_hi = 2.0 * (vec2(pixel) + 1.0) / float(previous_size) - 1.0;
            float weight = solid_angle(max(source_lo, destination_lo),
                                       min(source_hi, destination_hi));
            sum += textureCubeLod(s_source,
                                  bake_direction(pixel, ivec2(previous_size, previous_size)),
                                  u_bake_face.y - 1.0).rgb * weight;
            weight_sum += weight;
        }
    }
    imageStore(s_result, id, vec4(sum / max(weight_sum, 1e-8), 1.0));
}
