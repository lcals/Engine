/*
 * Adapted from bgfx 15-shadowmaps-simple (packed depth and PCF) and
 * examples/common/shaderlib.sh (packing and ACES fit), BSD-2-Clause.
 * Copyright 2013-2014 Dario Manesku; Copyright 2010-2025 Branimir Karadzic.
 * See third_party/notices/bgfx.txt. Changes: explicit engine uniforms,
 * world-space coordinates, reserved sampler slot and configurable bias.
 * Encoding uses radix 255 to match RGBA8 UNORM's 255 intervals. The example's
 * radix 256 leaves up to 0.00197 depth error after UNORM quantization, producing
 * self-shadow stripes even on a planar receiver.
 */

vec4 pack_shadow_depth(float value)
{
    const vec4 shift = vec4(16581375.0, 65025.0, 255.0, 1.0);
    const vec4 mask = vec4(0.0, 1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0);
    vec4 packed = fract(value * shift);
    return packed - packed.xxyz * mask;
}

float unpack_shadow_depth(vec4 value)
{
    return dot(value, vec4(1.0 / 16581375.0, 1.0 / 65025.0, 1.0 / 255.0, 1.0));
}

vec3 aces_filmic(vec3 color)
{
    return saturate((color * (2.51 * color + 0.03)) /
                    (color * (2.43 * color + 0.59) + 0.14));
}
