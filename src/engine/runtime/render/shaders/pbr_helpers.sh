/*
 * Copyright (c) 2017-2025 The Forge Interactive Inc.
 * Adapted from Common_3/Graphics/ShaderUtilities.h.fsl, Apache-2.0.
 * https://github.com/ConfettiFX/The-Forge
 * License and attribution: third_party/notices/the-forge.txt.
 * Changes: bgfx shader syntax, explicit scalar expansion; metallic-roughness
 * follows glTF 2.0 Appendix B (Khronos Group, CC BY 4.0):
 * https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc
 * IBL Fresnel and multiple scattering adapted from Khronos glTF Sample Renderer,
 * Apache-2.0; see third_party/notices/gltf-sample-renderer.md.
 */

// Ray Tracing Gems, Ch. 12 (2019 NVIDIA, MIT): diffuse bump-terminator
// shadowing, not an additional masking term for the specular microfacet lobe.
// Fixed source and full license: third_party/notices/ray-tracing-gems-terminator.md.
// The caller rejects the back hemisphere. Both normals must be unit length.
float pbr_bump_shadowing(vec3 base_normal, vec3 normal, vec3 light_dir)
{
    float cos_delta = saturate(abs(dot(base_normal, normal)));
    float cos_delta_squared = cos_delta * cos_delta;
    float alpha_squared = saturate((1.0 - cos_delta_squared) /
                                   max(8.0 * cos_delta_squared, 0.00000001));
    if (alpha_squared == 0.0) return 1.0;
    float cos_incident = saturate(abs(dot(base_normal, light_dir)));
    // Equivalent to 2/(1+sqrt(1+alpha^2*tan^2(theta))), but finite at grazing.
    return 2.0 * cos_incident /
           (cos_incident + sqrt(alpha_squared + (1.0 - alpha_squared) *
                                                cos_incident * cos_incident));
}

// Fdez-Aguera approximation as used by glTF Sample Renderer getIBLGGXFresnel.
// lut contains the two pre-integrated correlated Smith GGX terms.
vec3 pbr_ibl_fresnel(vec3 f0, float roughness, float n_dot_v, vec2 lut)
{
    vec3 fr = max(vec3_splat(1.0 - roughness), f0) - f0;
    vec3 ks = f0 + fr * pow(1.0 - n_dot_v, 5.0);
    vec3 single = ks * lut.x + vec3_splat(lut.y);
    float ems = 1.0 - (lut.x + lut.y);
    vec3 average = f0 + (vec3_splat(1.0) - f0) / 21.0;
    vec3 multiple = ems * single * average / max(vec3_splat(1.0) - average * ems,
                                                 vec3_splat(0.000001));
    return single + multiple;
}

float pbr_distribution(float alpha_squared, float n_dot_h)
{
    // Algebraically equivalent GGX form avoids cancellation at smooth peaks.
    // Surface roughness is clamped to 0.045, so the denominator stays nonzero.
    float d = alpha_squared + (1.0 - alpha_squared) * (1.0 - n_dot_h * n_dot_h);
    return alpha_squared / (3.14159265 * d * d);
}

float pbr_visibility(float alpha_squared, float n_dot_v, float n_dot_l)
{
    // Height-correlated Smith GGX visibility from the glTF 2.0 Appendix B.
    float v = n_dot_l * sqrt(n_dot_v * n_dot_v * (1.0 - alpha_squared) + alpha_squared);
    float l = n_dot_v * sqrt(n_dot_l * n_dot_l * (1.0 - alpha_squared) + alpha_squared);
    return 0.5 / max(v + l, 0.0000001);
}

vec3 pbr_fresnel(vec3 specular_color, float v_dot_h)
{
    float facing = pow(1.0 - v_dot_h, 5.0);
    return vec3_splat(facing) + (1.0 - facing) * specular_color;
}

vec3 pbr_specular(float roughness, vec3 specular_color,
                  float n_dot_l, float n_dot_v, float n_dot_h, float v_dot_h)
{
    float alpha = roughness * roughness;
    float alpha_squared = alpha * alpha;
    float distribution = pbr_distribution(alpha_squared, n_dot_h);
    float visibility = pbr_visibility(alpha_squared, n_dot_v, n_dot_l);
    return (distribution * visibility) * pbr_fresnel(specular_color, v_dot_h);
}
