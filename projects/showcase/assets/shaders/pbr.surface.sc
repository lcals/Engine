$input v_world_position, v_world_normal, v_world_tangent, v_texcoord0, v_color0, v_lightmap_uv
#include "pbr_surface.sh"
uniform vec4 u_camera_position;
void main()
{
    vec3 view_dir = pbr_view_direction(u_camera_position.xyz, v_world_position);
    PbrSurface surface = sample_pbr_surface(v_texcoord0, v_color0, v_world_normal,
                                          v_world_tangent, view_dir);
    gl_FragData[0] = vec4(surface.normal, surface.roughness);
    gl_FragData[1] = vec4(surface.specular_color,
                         u_material.z > 0.5 && u_material.z < 1.5 ? 0.0 : 1.0);
}
