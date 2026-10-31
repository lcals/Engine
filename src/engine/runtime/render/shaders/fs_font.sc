$input v_font_coord, v_font_range, v_color0

// Quadratic coverage adapted from the public Slug reference shader:
// https://github.com/bkaradzic/bgfx/blob/a7016487e5970c5299bb837f1af42dcb24909a67/examples/51-gpufont/fs_slug.sc
// The curve storage is tightly packed and shared across glyphs. This version
// traverses one glyph's curves, keeps premultiplied colour and supports the
// renderer's ordinary depth, transform and scissor state.
//
// MIT License
// Copyright (c) 2017 Eric Lengyel
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <bgfx_shader.sh>

SAMPLER2D(s_font_curves, 0);
// Curve texture size (xy), linear HDR output enabled (z), reserved (w).
uniform vec4 u_font_texture;

vec4 curve_texel(int index)
{
    int width = int(u_font_texture.x);
    return texelFetch(s_font_curves, ivec2(index % width, index / width), 0);
}

int root_code(float a, float b, float c)
{
    int signs = (a < 0.0 ? 1 : 0) + (b < 0.0 ? 2 : 0) + (c < 0.0 ? 4 : 0);
    return (0x2e74 >> signs) & 0x0101;
}

vec2 horizontal_roots(vec4 p01, vec2 p2)
{
    vec2 a = p01.xy - 2.0 * p01.zw + p2;
    vec2 b = p01.xy - p01.zw;
    float t1;
    float t2;
    if (abs(a.y) < 1.0 / 65536.0)
    {
        t1 = p01.y * 0.5 / b.y;
        t2 = t1;
    }
    else
    {
        float discriminant = sqrt(max(b.y * b.y - a.y * p01.y, 0.0));
        t1 = (b.y - discriminant) / a.y;
        t2 = (b.y + discriminant) / a.y;
    }
    return vec2((a.x * t1 - 2.0 * b.x) * t1 + p01.x,
                (a.x * t2 - 2.0 * b.x) * t2 + p01.x);
}

void main()
{
    vec2 pixels_per_em = 1.0 / max(fwidth(v_font_coord), vec2(0.0000001, 0.0000001));
    float x_coverage = 0.0;
    float y_coverage = 0.0;
    float x_weight = 0.0;
    float y_weight = 0.0;
    int offset = int(v_font_range.x + 0.5);
    int count = int(v_font_range.y + 0.5);
    for (int index = 0; index < count; ++index)
    {
        int location = (offset + index) * 2;
        vec4 p01 = curve_texel(location) - vec4(v_font_coord, v_font_coord);
        vec2 p2 = curve_texel(location + 1).xy - v_font_coord;
        int horizontal = root_code(p01.y, p01.w, p2.y);
        if (horizontal != 0 && max(max(p01.x, p01.z), p2.x) * pixels_per_em.x >= -0.5)
        {
            vec2 roots = horizontal_roots(p01, p2) * pixels_per_em.x;
            if ((horizontal & 1) != 0)
            {
                x_coverage += clamp(roots.x + 0.5, 0.0, 1.0);
                x_weight = max(x_weight, clamp(1.0 - abs(roots.x) * 2.0, 0.0, 1.0));
            }
            if (horizontal > 1)
            {
                x_coverage -= clamp(roots.y + 0.5, 0.0, 1.0);
                x_weight = max(x_weight, clamp(1.0 - abs(roots.y) * 2.0, 0.0, 1.0));
            }
        }
        int vertical = root_code(p01.x, p01.z, p2.x);
        if (vertical != 0 && max(max(p01.y, p01.w), p2.y) * pixels_per_em.y >= -0.5)
        {
            vec2 roots = horizontal_roots(p01.yxwz, p2.yx) * pixels_per_em.y;
            if ((vertical & 1) != 0)
            {
                y_coverage -= clamp(roots.x + 0.5, 0.0, 1.0);
                y_weight = max(y_weight, clamp(1.0 - abs(roots.x) * 2.0, 0.0, 1.0));
            }
            if (vertical > 1)
            {
                y_coverage += clamp(roots.y + 0.5, 0.0, 1.0);
                y_weight = max(y_weight, clamp(1.0 - abs(roots.y) * 2.0, 0.0, 1.0));
            }
        }
    }
    float weighted = abs(x_coverage * x_weight + y_coverage * y_weight) /
                     max(x_weight + y_weight, 1.0 / 65536.0);
    float coverage = clamp(max(weighted, min(abs(x_coverage), abs(y_coverage))), 0.0, 1.0);
    vec4 color = v_color0;
    if (u_font_texture.z > 0.5)
    {
        // Font vertices carry premultiplied display colors. Decode the straight color,
        // then premultiply again; coverage and alpha always remain linear quantities.
        vec3 straight_color = saturate(color.rgb / max(color.a, 0.00001));
        color.rgb = pow(straight_color, vec3_splat(2.2)) * color.a;
    }
    gl_FragColor = color * coverage;
}
