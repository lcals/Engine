/*
 * Copyright (c) 2017-2025 The Forge Interactive Inc.
 * Adapted from Common_3/Graphics/ShaderUtilities.h.fsl, Apache-2.0.
 * https://github.com/ConfettiFX/The-Forge
 * License and attribution: third_party/notices/the-forge.txt.
 * Changes: bgfx shader syntax, explicit scalar expansion, denominator guard.
 */

float pbr_distribution(float alpha_squared, float n_dot_h)
{
    float denominator = (n_dot_h * alpha_squared - n_dot_h) * n_dot_h + 1.0;
    return alpha_squared / max(3.14159265 * denominator * denominator, 0.000001);
}

float pbr_visibility(float alpha_squared, float n_dot_v, float n_dot_l)
{
    float alpha = sqrt(alpha_squared);
    float visibility_v = n_dot_l * (n_dot_v * (1.0 - alpha) + alpha);
    float visibility_l = n_dot_v * (n_dot_l * (1.0 - alpha) + alpha);
    return 0.5 / max(visibility_v + visibility_l, 0.01);
}

vec3 pbr_fresnel(vec3 specular_color, float v_dot_h)
{
    float facing = pow(1.0 - v_dot_h, 5.0);
    return vec3_splat(saturate(specular_color.g) * facing) + (1.0 - facing) * specular_color;
}

vec3 pbr_specular(float roughness, inout vec3 specular_color,
                  float n_dot_l, float n_dot_v, float n_dot_h, float v_dot_h)
{
    float alpha = roughness * roughness;
    float alpha_squared = alpha * alpha;
    float distribution = pbr_distribution(alpha_squared, n_dot_h);
    float visibility = pbr_visibility(alpha_squared, n_dot_v, n_dot_l);
    specular_color = pbr_fresnel(specular_color, v_dot_h);
    return (distribution * visibility) * specular_color;
}
