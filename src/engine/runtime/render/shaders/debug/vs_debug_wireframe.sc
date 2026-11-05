$output v_color

/*
 * Vertex pulling and screen-space edge expansion adapted from bgfx 28-wireframe,
 * b2513038f4b753ebeced40821c193978daa0492f.
 * Copyright 2026 Alessandro Muntoni. BSD-2-Clause; third_party/notices/bgfx-debugdraw.md.
 * Engine buffers use uint32 indices and the actual packed vertex stride.
 */
#include <bgfx_shader.sh>
#include <bgfx_compute.sh>

BUFFER_RAW_RO(positions, 0);
BUFFER_RAW_RO(indices, 1);
uniform vec4 u_debug_wire_parameters;
uniform vec4 u_debug_wire_color;

vec3 positionAt(uint index)
{
    uint offset = index * uint(u_debug_wire_parameters.x);
    return vec3(rawLoadFloat(positions, offset), rawLoadFloat(positions, offset + 1u),
                rawLoadFloat(positions, offset + 2u));
}

void main()
{
    uint lineIndex = gl_VertexID / 6u;
    uint edgeIndex = lineIndex % 3u;
    uint triFirst = lineIndex - edgeIndex;
    uint a = rawLoadUint(indices, triFirst + edgeIndex);
    uint b = rawLoadUint(indices, triFirst + ((edgeIndex + 1u) % 3u));
    vec4 p0 = mul(u_modelViewProj, vec4(positionAt(a), 1.0));
    vec4 p1 = mul(u_modelViewProj, vec4(positionAt(b), 1.0));
    const float nearEpsilon = 0.001;
    if (p0.w <= nearEpsilon && p1.w <= nearEpsilon)
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        v_color = vec4_splat(0.0);
    }
    else
    {
        if (p0.w < nearEpsilon)
        {
            p0 = mix(p0, p1, (nearEpsilon - p0.w) / (p1.w - p0.w));
        }
        if (p1.w < nearEpsilon)
        {
            p1 = mix(p1, p0, (nearEpsilon - p1.w) / (p0.w - p1.w));
        }
        const vec2 offsets[6] =
        {
            vec2(-1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, -1.0),
            vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0)
        };
        vec2 offset = offsets[gl_VertexID % 6u];
        vec4 p = mix(p0, p1, offset.x * 0.5 + 0.5);
        vec2 direction = p1.xy / p1.w - p0.xy / p0.w;
        float magnitude = length(direction);
        if (magnitude > 0.0001)
        {
            vec2 normal = vec2(-direction.y, direction.x) / magnitude;
            p.xy += normal * (0.5 * u_debug_wire_parameters.y) * offset.y *
                    (2.0 / u_viewRect.zw) * p.w;
        }
        gl_Position = p;
        v_color = u_debug_wire_color;
    }
}
