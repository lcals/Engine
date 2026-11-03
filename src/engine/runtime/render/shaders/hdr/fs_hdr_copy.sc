$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_hdr_scene, 0);

void main()
{
    // Copy resolved linear radiance without another exposure or color conversion.
    gl_FragColor = texture2D(s_hdr_scene, v_texcoord0);
}
