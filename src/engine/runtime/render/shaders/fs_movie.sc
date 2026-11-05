$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_movie_y, 0);
SAMPLER2D(s_movie_u, 1);
SAMPLER2D(s_movie_v, 2);
uniform vec4 u_movie_color;
uniform vec4 u_movie_chroma;
void main()
{
    float y = texture2D(s_movie_y, v_texcoord0).r;
    vec2 uv = v_texcoord0 + u_movie_chroma.xy;
    vec4 chroma = texture2D(s_movie_u, uv);
    float u = chroma.r;
    float v = u_movie_color.w > 0.5 ? chroma.g : texture2D(s_movie_v, uv).r;
    if (u_movie_color.y > 0.5) {
        y = (y * 255.0 - 16.0) / 219.0;
        u = (u * 255.0 - 128.0) / 224.0;
        v = (v * 255.0 - 128.0) / 224.0;
    } else {
        u -= 0.5;
        v -= 0.5;
    }
    vec3 rgb;
    if (u_movie_color.x > 0.5)
        rgb = vec3(y + 1.402 * v, y - 0.344136 * u - 0.714136 * v, y + 1.772 * u);
    else
        rgb = vec3(y + 1.5748 * v, y - 0.187324 * u - 0.468124 * v, y + 1.8556 * u);
    rgb = clamp(rgb, 0.0, 1.0);
    if (u_movie_color.z > 0.5) {
        vec3 linear_rgb = mix(rgb / 4.5, pow((rgb + 0.099) / 1.099, vec3(1.0 / 0.45)), step(vec3(0.081), rgb));
        rgb = mix(linear_rgb * 12.92, 1.055 * pow(linear_rgb, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), linear_rgb));
    }
    gl_FragColor = vec4(rgb, 1.0);
}
