#define LIGHTMAPPER_IMPLEMENTATION
#include <lightmapper.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <vector>

namespace
{
bool integrate(float validity, int interpolation)
{
    std::unique_ptr<lm_context, decltype(&lmDestroy)> context(
        lmCreate(16, 0.01F, 10, 0, 0, 0, interpolation, 0.01F, 0), lmDestroy);
    if (!context)
    {
        return false;
    }
    float lightmap[8 * 8 * 4]{};
    constexpr float positions[]{0, 0, 0, 1, 0, 0, 0, 1, 0};
    constexpr float normals[]{0, 0, 1, 0, 0, 1, 0, 0, 1};
    constexpr float uv[]{0.1F, 0.1F, 0.9F, 0.1F, 0.1F, 0.9F};
    constexpr unsigned indices[]{0, 1, 2};
    lmSetTargetLightmap(context.get(), lightmap, 8, 8, 4);
    lmSetGeometry(context.get(), nullptr, LM_FLOAT, positions, 12, LM_FLOAT, normals, 12, LM_FLOAT,
                  uv, 8, 3, LM_UNSIGNED_INT, indices);
    int viewport[4]{};
    float view[16]{}, projection[16]{};
    unsigned captures = 0, texels = 0;
    double sum = 0;
    while (lmBegin(context.get(), viewport, view, projection))
    {
        std::vector<float> rgba(viewport[2] * viewport[3] * 4, 1.0F);
        for (std::size_t i = 3; i < rgba.size(); i += 4)
        {
            rgba[i] = validity;
        }
        lmSubmitPixels(context.get(), rgba.data(), viewport[2], viewport[3]);
        lmEnd(context.get());
        if (++captures > 10000)
        {
            return false;
        }
    }
    for (unsigned i = 0; i < 64; ++i)
    {
        if (lightmap[i * 4 + 3] > 0.5F)
        {
            ++texels;
            sum += lightmap[i * 4];
        }
    }
    if (validity == 0)
    {
        return texels == 0 && captures > 0;
    }
    const double value = texels ? sum / texels : 0;
    std::printf("lightmapper: interpolation=%d captures=%u texels=%u E=%.6f expected=pi\n",
                interpolation, captures, texels, value);
    return texels > 0 && std::abs(value - std::numbers::pi) < 0.02;
}
} // namespace

int main()
{
    return integrate(1, 0) && integrate(1, 2) && integrate(0, 0) ? 0 : 1;
}
