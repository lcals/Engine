/*
 * Adapted from bgfx examples/39-assao at
 * 7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099. Fixed quality 2, two smart blur passes.
 * Changes: private uniforms, orthographic reconstruction, safe border reads,
 * and linear visibility for forward indirect lighting. The sampling pattern,
 * depth mip weighting, edge detection and smart filtering remain upstream.
 * License and change details: third_party/notices/bgfx-assao.md.
 */

uniform vec4 u_assao_params[19];
uniform vec4 u_assao_rect;

// Perspective (0) or orthographic (1) view reconstruction.
#define u_orthographic u_assao_params[1].z

#define u_viewportPixelSize					u_assao_params[0].xy
#define u_halfViewportPixelSize				u_assao_params[0].zw
#define u_depthUnpackConsts					u_assao_params[1].xy
#define u_ndcToViewMul						u_assao_params[2].xy
#define u_ndcToViewAdd						u_assao_params[2].zw
#define u_perPassFullResCoordOffset			u_assao_params[3].xy
#define u_perPassFullResUVOffset			u_assao_params[3].zw
#define u_viewport2xPixelSize				u_assao_params[4].xy
#define u_viewport2xPixelSize_x_025			u_assao_params[4].zw
#define u_effectRadius						u_assao_params[5].x
#define u_effectShadowStrength				u_assao_params[5].y
#define u_effectShadowPow					u_assao_params[5].z
#define u_effectShadowClamp					u_assao_params[5].w
#define u_effectFadeOutMul					u_assao_params[6].x
#define u_effectFadeOutAdd					u_assao_params[6].y
#define u_effectHorizonAngleThreshold		u_assao_params[6].z
#define u_effectSamplingRadiusNearLimitRec	u_assao_params[6].w
#define u_depthPrecisionOffsetMod			u_assao_params[7].x
#define u_negRecEffectRadius				u_assao_params[7].y
#define u_loadCounterAvgDiv					u_assao_params[7].z
#define u_adaptiveSampleCountLimit			u_assao_params[7].w
#define u_invSharpness						u_assao_params[8].x
#define u_passIndex							u_assao_params[8].y
#define u_quarterResPixelSize				u_assao_params[8].zw
#define u_patternRotScaleMatrices(i)		u_assao_params[9+(i)]
#define u_normalsUnpackMul					u_assao_params[14].x
#define u_normalsUnpackAdd					u_assao_params[14].y
#define u_detailAOStrength					u_assao_params[14].z
#define u_layer								u_assao_params[14].w
#define u_normalsWorldToViewspaceMatrix0	u_assao_params[15]
#define u_normalsWorldToViewspaceMatrix1	u_assao_params[16]
#define u_normalsWorldToViewspaceMatrix2	u_assao_params[17]
#define u_normalsWorldToViewspaceMatrix3	u_assao_params[18]

#define SSAO_MAX_TAPS                               32
#define SSAO_ADAPTIVE_TAP_BASE_COUNT                5
#define SSAO_ADAPTIVE_TAP_FLEXIBLE_COUNT            (SSAO_MAX_TAPS-SSAO_ADAPTIVE_TAP_BASE_COUNT)
#define SSAO_DEPTH_MIP_LEVELS                       4
#define SSAO_ENABLE_NORMAL_WORLD_TO_VIEW_CONVERSION 1
