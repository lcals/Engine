/*
 * Adapted from bgfx examples/09-hdr/common.sh and examples/common/shaderlib.sh.
 * Copyright 2011-2025 Branimir Karadzic. BSD-2-Clause.
 * See third_party/notices/bgfx-hdr.md for the source and license.
 * Preserve blur9 weights; intermediate scene and Bloom values remain linear RGBA16F.
 */

#include <bgfx_shader.sh>

// Exposure, ACES enabled, Bloom intensity, pre-exposure linear Bloom threshold.
uniform vec4 u_hdr_params;
// Sampling interval: source texels for extraction, scaled Bloom texels for blur.
uniform vec4 u_hdr_texel;

vec3 hdr_blur9(sampler2D source, vec2 uv, vec2 step_size)
{
    vec3 color = texture2D(source, uv).rgb;
    color += (texture2D(source, uv - step_size).rgb
            + texture2D(source, uv + step_size).rgb) * 0.9;
    color += (texture2D(source, uv - step_size * 2.0).rgb
            + texture2D(source, uv + step_size * 2.0).rgb) * 0.55;
    color += (texture2D(source, uv - step_size * 3.0).rgb
            + texture2D(source, uv + step_size * 3.0).rgb) * 0.18;
    color += (texture2D(source, uv - step_size * 4.0).rgb
            + texture2D(source, uv + step_size * 4.0).rgb) * 0.1;
    return color / 4.46;
}

vec3 hdr_display_color(vec3 radiance)
{
    vec3 color = max(radiance, vec3_splat(0.0)) * u_hdr_params.x;
    if (u_hdr_params.y > 0.5)
    {
        color = saturate((color * (2.51 * color + 0.03)) /
                         (color * (2.43 * color + 0.59) + 0.14));
    }
    else
    {
        color = color / (vec3_splat(1.0) + color);
    }
    // The current swapchain is SDR and does not perform an additional sRGB encoding.
    return pow(max(color, vec3_splat(0.0)), vec3_splat(1.0 / 2.2));
}
