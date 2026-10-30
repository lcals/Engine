// Adapted from Ant (MIT), Copyright (c) 2017-2024 lingxigames.com.
// See third_party/notices/ant-scene-bake.md for upstream attribution and changes.
#include "ibl_common.sh"
SAMPLER2D(s_source, 0);
IMAGE2D_WO(s_result, rgba16f, 1);
NUM_THREADS(WORKGROUP_THREADS, WORKGROUP_THREADS, 1)
void main()
{
    ivec2 size = imageSize(s_result);
    if (any(gl_GlobalInvocationID.xy >= size)) return;
    vec3 direction = bake_direction(ivec2(gl_GlobalInvocationID.xy), size);
    vec2 uv = vec2(0.5 + 0.5 * atan2(direction.z, direction.x) / M_PI,
                   acos(clamp(direction.y, -1.0, 1.0)) / M_PI);
    vec3 color = texture2DLod(s_source, uv, u_bake_face.y).rgb;
    imageStore(s_result, ivec2(gl_GlobalInvocationID.xy), vec4(color, 1.0));
}
