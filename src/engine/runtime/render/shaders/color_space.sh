/*
 * Accurate sRGB conversion adapted from bgfx examples/common/shaderlib.sh,
 * commit 7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099.
 * Copyright 2011-2025 Branimir Karadzic. BSD-2-Clause.
 * See third_party/notices/bgfx-hdr.md. Renamed to avoid collisions.
 */
#ifndef ENGINE_COLOR_SPACE_SH
#define ENGINE_COLOR_SPACE_SH

vec3 display_srgb_to_linear(vec3 color)
{
    vec3 lo = color / 12.92;
    vec3 hi = pow((color + 0.055) / 1.055, vec3_splat(2.4));
    return mix(hi, lo, vec3(lessThanEqual(color, vec3_splat(0.04045))));
}

vec3 linear_to_display_srgb(vec3 color)
{
    vec3 lo = color * 12.92;
    vec3 hi = pow(abs(color), vec3_splat(1.0 / 2.4)) * 1.055 - 0.055;
    return mix(hi, lo, vec3(lessThanEqual(color, vec3_splat(0.0031308))));
}

#endif
