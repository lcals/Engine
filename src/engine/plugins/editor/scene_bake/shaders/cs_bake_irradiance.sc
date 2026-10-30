// Adapted from Ant (MIT), Copyright (c) 2017-2024 lingxigames.com.
// See third_party/notices/ant-scene-bake.md for upstream attribution and changes.
#include "ibl_common.sh"

SAMPLERCUBE(s_source, 0);

IMAGE2D_WO(s_irradiance_write, rgba16f, 1);

NUM_THREADS(WORKGROUP_THREADS, WORKGROUP_THREADS, 1)
void main()
{
    vec3 color = vec3_splat(0.0);
    ivec2 isize = imageSize(s_irradiance_write);
    if (any(gl_GlobalInvocationID.xy >= isize)) return;
    vec3 N = bake_direction(ivec2(gl_GlobalInvocationID.xy), isize);

    for (int sampleidx=0; sampleidx < int(u_sample_count); ++sampleidx){
        vec4 H = importance_sample_Lambertian(sampleidx, N);
        vec3 H_dir = H.xyz;
        float pdf = H.w;

        float lod = compute_lod(pdf);
        lod += u_lod_bias;

        // sample lambertian at a lower resolution to avoid fireflies
        vec3 lambertian = textureCubeLod(s_source, H_dir, lod).rgb;

        //// the below operations cancel each other out
        // float NdotH = clamp(dot(N, H), 0.0, 1.0);
        // lambertian *= NdotH;     // lamberts law
        // lambertian /= pdf;       // invert bias from importance sampling
        // lambertian /= M_PI;   // convert irradiance to radiance https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/

        color += lambertian;
    }

    color /= u_sample_count;

    imageStore(s_irradiance_write, ivec2(gl_GlobalInvocationID.xy), vec4(color, 1.0));
}
