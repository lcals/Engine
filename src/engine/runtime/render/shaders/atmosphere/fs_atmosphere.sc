// Changes: linear HDR MRT output, no example exposure/tonemap; homogeneous depth.
// See third_party/notices/bgfx-atmosphere-hex.md.
$input v_texcoord0

/*
* Copyright 2026 Mateusz Kozdrowicki. All rights reserved.
* License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
*/

// Reference: Hillaire, Sébastien. "A Scalable and Production Ready Sky and Atmosphere Rendering Technique.
// https://sebh.github.io/publications/egsr2020.pdf

#include <bgfx_shader.sh>
#include "atmosphere.sh"


SAMPLER2D(s_atmosphere_skyview,           0);
SAMPLER2D(s_atmosphere_transmittance, 1);

uniform mat4 u_atmosphere_inverse;
uniform vec4 u_atmosphere_sky;    // x: view radius (km), y: cos sun angular radius, z: sun disc scale
uniform vec4 u_atmosphere_sun_direction;
uniform vec4 u_atmosphere_radiance;
uniform vec4 u_atmosphere_clip; // x: near clip depth, y: scene display tone weight


// sun relative azimuth of dir
float sun_relative_azimuth(vec3 dir, vec3 to_sun) {
	vec2 d = vec2(dir.x, dir.z);
	vec2 s = vec2(to_sun.x, to_sun.z);

	float dl = length(d);
	float sl = length(s);
	if (dl < 1e-5 || sl < 1e-5)
		return 0.0;

	d /= dl;
	s /= sl;

	return atan2(d.x * s.y - d.y * s.x, dot(d, s));
}

void main() {
	// reconstruct the world space view ray
	vec4 nearp = mul(u_atmosphere_inverse, vec4(v_texcoord0.xy, u_atmosphere_clip.x, 1.0));
	vec4 farp  = mul(u_atmosphere_inverse, vec4(v_texcoord0.xy, 1.0, 1.0));
	vec3 eye   = nearp.xyz / nearp.w;
	vec3 at    = farp.xyz  / farp.w;
	vec3 dir   = normalize(at - eye);

	float r      = u_atmosphere_sky.x;
	vec3  to_sun = -normalize(u_atmosphere_sun_direction.xyz);

	vec2 uv = atmosphere_skyview_uv(r, dir.y, sun_relative_azimuth(dir, to_sun));
	vec3 radiance = texture2DLod(s_atmosphere_skyview, uv, 0.0).rgb;

	// Sun disc, circumsolar halo
	float cos_view_sun = dot(dir, to_sun);
	if (!atmosphere_hits_ground(r, dir.y)) {
		vec3 transmittance = texture2DLod(s_atmosphere_transmittance, atmosphere_transmittance_uv(r, dir.y), 0.0).rgb;

		float halo = exp2(-(1.0 - cos_view_sun) * 12000.0);
		radiance += u_atmosphere_radiance.rgb * transmittance * (u_atmosphere_sky.z * 0.03) * halo;

		if (cos_view_sun >= u_atmosphere_sky.y) {
			float x2   = saturate((1.0 - cos_view_sun) / max(1.0 - u_atmosphere_sky.y, 1e-7));
			float limb = pow(max(1.0 - x2, 0.0), 0.3);
			radiance += u_atmosphere_radiance.rgb * u_atmosphere_sky.z * transmittance * limb;
		}
	}

	gl_FragData[0] = vec4(clamp(radiance, vec3_splat(0.0), vec3_splat(65504.0)), u_atmosphere_clip.y);
	gl_FragData[1] = vec4_splat(0.0);
}
