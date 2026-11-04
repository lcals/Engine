#ifndef ENGINE_HEX_TILING_SH
#define ENGINE_HEX_TILING_SH

// bgfx b2513038f4b753ebeced40821c193978daa0492f, 49-hextile / 53-sky2.
// Normal derivatives adapted from mmikk/hextile-demo 43c3ed7 (MIT, Copyright 2022 mmikk).
// Changes: shared PBR weights/gradients and engine XYZ normal convention.
// License: third_party/notices/bgfx-atmosphere-hex.md.
// Hextiling
// Adapted from bgfx example 49-hextile (Copyright 2022 Preetish Kakkar).

#define HEXTILE_PI 3.14159265359

vec3 hextile_gain3(vec3 x, float r) {
	float k = log(1.0 - r) / log(0.5);

	vec3 s = 2.0 * step(0.5, x);
	vec3 m = 2.0 * (1.0 - s);

	vec3 res = 0.5 * s + 0.25 * m * pow(max(vec3_splat(0.0), s + x * m), vec3_splat(k) );

	return res.xyz / (res.x + res.y + res.z);
}

mat2 hextile_rot2x2(vec2 idx, float rotStrength) {
	float angle = abs(idx.x * idx.y) + abs(idx.x + idx.y) + HEXTILE_PI;

	if (angle < 0.0)        angle += 2.0 * HEXTILE_PI;
	if (angle > HEXTILE_PI) angle -= 2.0 * HEXTILE_PI;

	angle *= rotStrength;

	float cs = cos(angle);
	float si = sin(angle);

	return mat2(cs, -si, si, cs);
}

vec2 hextile_cen_st(vec2 vertex) {
	mat2 invSkewMat = mat2(1.0, 0.5, 0.0, 1.0 / 1.15470054);

	return mul(invSkewMat, vertex) / (2.0 * sqrt(3.0) );
}

vec2 hextile_hash(vec2 p) {
	vec2 r = mul(mat2(127.1, 311.7, 269.5, 183.3), p);

	return fract(sin(r) * 43758.5453);
}

void hextile_grid(
	  out float w1, out float w2, out float w3
	, out vec2 vertex1, out vec2 vertex2, out vec2 vertex3
	, vec2 uv
	) {
	uv *= 2.0 * sqrt(3.0);

	const mat2 gridToSkewedGrid = mat2(1.0, -0.57735027, 0.0, 1.15470054);
	vec2 skewedCoord = mul(gridToSkewedGrid, uv);

	vec2 baseId = floor(skewedCoord);
	vec3 temp   = vec3(fract(skewedCoord), 0.0);
	temp.z = 1.0 - temp.x - temp.y;

	float s  = step(0.0, -temp.z);
	float s2 = 2.0 * s - 1.0;

	w1 = -temp.z * s2;
	w2 = s - temp.y * s2;
	w3 = s - temp.x * s2;

	vertex1 = baseId + vec2(s, s);
	vertex2 = baseId + vec2(s, 1.0 - s);
	vertex3 = baseId + vec2(1.0 - s, s);
}

void hextile_setup(
	  vec2 uv, float rotStrength
	, out vec2 uv1,  out vec2 uv2,  out vec2 uv3
	, out mat2 rot1, out mat2 rot2, out mat2 rot3
	, out vec3 bary
	) {
	float w1, w2, w3;
	vec2 vertex1, vertex2, vertex3;
	hextile_grid(w1, w2, w3, vertex1, vertex2, vertex3, uv);

	rot1 = hextile_rot2x2(vertex1, rotStrength);
	rot2 = hextile_rot2x2(vertex2, rotStrength);
	rot3 = hextile_rot2x2(vertex3, rotStrength);

	vec2 cen1 = hextile_cen_st(vertex1);
	vec2 cen2 = hextile_cen_st(vertex2);
	vec2 cen3 = hextile_cen_st(vertex3);

	uv1 = mul(uv - cen1, rot1) + cen1 + hextile_hash(vertex1);
	uv2 = mul(uv - cen2, rot2) + cen2 + hextile_hash(vertex2);
	uv3 = mul(uv - cen3, rot3) + cen3 + hextile_hash(vertex3);

	bary = vec3(w1, w2, w3);
}

struct HexTilingContext
{
    vec2 uv1; vec2 uv2; vec2 uv3;
    mat2 rot1; mat2 rot2; mat2 rot3;
    vec2 dx; vec2 dy;
    vec3 weights;
};

HexTilingContext hextile_context(vec2 uv, float rotation, float contrast, sampler2D base_source,
                                  out vec4 base_sample)
{
    HexTilingContext result;
    vec3 bary;
    result.dx = dFdx(uv);
    result.dy = dFdy(uv);
    hextile_setup(uv, rotation, result.uv1, result.uv2, result.uv3,
                   result.rot1, result.rot2, result.rot3, bary);
    vec4 c1 = texture2DGrad(base_source, result.uv1, mul(result.dx, result.rot1), mul(result.dy, result.rot1));
    vec4 c2 = texture2DGrad(base_source, result.uv2, mul(result.dx, result.rot2), mul(result.dy, result.rot2));
    vec4 c3 = texture2DGrad(base_source, result.uv3, mul(result.dx, result.rot3), mul(result.dy, result.rot3));
    // bgfx 49's contrast-preserving weights, shared by all material channels.
    vec3 luminance = vec3(dot(c1.rgb, vec3(0.299, 0.587, 0.114)),
                          dot(c2.rgb, vec3(0.299, 0.587, 0.114)),
                          dot(c3.rgb, vec3(0.299, 0.587, 0.114)));
    vec3 weights = mix(vec3_splat(1.0), luminance, 0.6) * pow(max(bary, vec3_splat(0.0)), vec3_splat(7.0));
    weights /= max(dot(weights, vec3_splat(1.0)), 1e-12);
    result.weights = hextile_gain3(weights, contrast);
    base_sample = result.weights.x * c1 + result.weights.y * c2 + result.weights.z * c3;
    return result;
}

vec4 hextile_texture(sampler2D source, HexTilingContext tile)
{
    vec4 c1 = texture2DGrad(source, tile.uv1, mul(tile.dx, tile.rot1), mul(tile.dy, tile.rot1));
    vec4 c2 = texture2DGrad(source, tile.uv2, mul(tile.dx, tile.rot2), mul(tile.dy, tile.rot2));
    vec4 c3 = texture2DGrad(source, tile.uv3, mul(tile.dx, tile.rot3), mul(tile.dy, tile.rot3));
    return tile.weights.x * c1 + tile.weights.y * c2 + tile.weights.z * c3;
}

vec2 hextile_derivative(vec3 encoded)
{
    vec3 normal = encoded * 2.0 - 1.0;
    // mmikk TspaceNormalToDerivative: bounded slope, adapted to engine's +Y tangent normal.
    float z = max(abs(normal.z), (1.0 / 128.0) * max(abs(normal.x), abs(normal.y)));
    return -normal.xy / max(z, 1e-6);
}

vec3 hextile_normal(sampler2D source, HexTilingContext tile)
{
    vec2 d1 = hextile_derivative(texture2DGrad(source, tile.uv1, mul(tile.dx, tile.rot1), mul(tile.dy, tile.rot1)).xyz);
    vec2 d2 = hextile_derivative(texture2DGrad(source, tile.uv2, mul(tile.dx, tile.rot2), mul(tile.dy, tile.rot2)).xyz);
    vec2 d3 = hextile_derivative(texture2DGrad(source, tile.uv3, mul(tile.dx, tile.rot3), mul(tile.dy, tile.rot3)).xyz);
    // Chain rule: gradient transforms back from each sampled tile into the common UV basis.
    d1 = mul(tile.rot1, d1);
    d2 = mul(tile.rot2, d2);
    d3 = mul(tile.rot3, d3);
    vec2 derivative = tile.weights.x * d1 + tile.weights.y * d2 + tile.weights.z * d3;
    return normalize(vec3(-derivative, 1.0));
}
#endif // ENGINE_HEX_TILING_SH
