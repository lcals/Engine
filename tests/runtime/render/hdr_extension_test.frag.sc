#include <bgfx_shader.sh>

void main()
{
    // Exactly representable in RGBA16F, including a value above display white.
    gl_FragColor = vec4(2.0, 0.75, 0.125, 1.0);
}
