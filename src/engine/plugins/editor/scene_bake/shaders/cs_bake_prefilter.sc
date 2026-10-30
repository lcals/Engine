// Adapted from Ant (MIT), Copyright (c) 2017-2024 lingxigames.com.
// See third_party/notices/ant-scene-bake.md for upstream attribution and changes.
#include "ibl_common.sh"


SAMPLERCUBE(s_source, 0);

IMAGE2D_WO(s_prefilter_write, rgba16f, 1);

NUM_THREADS(WORKGROUP_THREADS, WORKGROUP_THREADS, 1)
void main()
{
    ivec2 isize = imageSize(s_prefilter_write);
    if (any(gl_GlobalInvocationID.xy >= isize.xy))
        return;

    vec4 color = vec4_splat(0.0);
    vec3 N = bake_direction(ivec2(gl_GlobalInvocationID.xy), isize);

    for (int sampleidx=0; sampleidx < int(u_sample_count); ++sampleidx){
        vec4 H = importance_sample_GGX(sampleidx, N, u_roughness);
        vec3 H_dir = H.xyz;
        float pdf = H.w;
        float lod = compute_lod(pdf);
        lod += u_lod_bias;

        // Note: reflect takes incident vector.
        vec3 V = N;
        vec3 L = normalize(reflect(-V, H_dir));
        float NdotL = dot(N, L);

        if (NdotL > 0.0)
        {
            if(u_roughness == 0.0)
            {
                // without this the roughness=0 lod is too high (taken from original implementation)
                lod = u_lod_bias;
            }
            color += vec4(textureCubeLod(s_source, L, lod).rgb * NdotL, NdotL);
        }
    }

    imageStore(s_prefilter_write, ivec2(gl_GlobalInvocationID.xy), vec4(color.rgb / max(color.a, 1e-6), 1.0));
}
