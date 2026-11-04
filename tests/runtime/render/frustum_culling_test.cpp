#include "runtime/render/render_math.hpp"

#include <bx/math.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
using namespace engine::render_detail;
constexpr std::array<float, 3> minimum{-0.5F, -0.5F, -0.5F};
constexpr std::array<float, 3> maximum{0.5F, 0.5F, 0.5F};

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "Frustum culling: %s\n", message);
    }
    return condition;
}

bool tests(bool homogeneous)
{
    float view[16], projection[16], model[16];
    bx::mtxIdentity(view);
    bx::mtxProj(projection, 60.0F, 1.0F, 0.1F, 100.0F, homogeneous, bx::Handedness::Right);
    FrustumPlanes planes;
    if (!expect(build_frustum_planes(view, projection, homogeneous, planes), "perspective planes"))
    {
        return false;
    }
    bx::mtxIdentity(model);
    model[14] = -5;
    if (!expect(aabb_visible(planes, model, minimum, maximum), "central mesh stays visible"))
    {
        return false;
    }
    model[12] = 100;
    if (!expect(!aabb_visible(planes, model, minimum, maximum),
                "horizontal offscreen mesh is culled"))
    {
        return false;
    }
    model[12] = 0;
    model[14] = 2;
    if (!expect(!aabb_visible(planes, model, minimum, maximum), "behind-eye mesh is culled"))
    {
        return false;
    }
    model[14] = -101;
    if (!expect(!aabb_visible(planes, model, minimum, maximum), "beyond-far mesh is culled"))
    {
        return false;
    }
    model[14] = 0.4F;
    if (!expect(aabb_visible(planes, model, minimum, maximum), "near-plane tangent retained"))
    {
        return false;
    }
    model[14] = -0.1F;
    if (!expect(aabb_visible(planes, model, minimum, maximum), "near-plane crossing retained"))
    {
        return false;
    }
    model[0] = -3;
    model[5] = 2;
    model[14] = -5;
    if (!expect(aabb_visible(planes, model, minimum, maximum),
                "mirror and nonuniform scale retained"))
    {
        return false;
    }
    model[12] = 100;
    model[0] = std::numeric_limits<float>::quiet_NaN();
    if (!expect(aabb_visible(planes, model, minimum, maximum),
                "nonfinite transform conservatively visible"))
    {
        return false;
    }
    bx::mtxIdentity(model);
    model[12] = 100;
    if (!expect(aabb_visible(planes, model, {1, 0, 0}, {-1, 0, 0}),
                "invalid bounds conservatively visible"))
    {
        return false;
    }
    // Off-centre projection used by capture/reflective cameras is not replaced by a symmetric FOV.
    projection[8] = 0.7F;
    model[12] = 0;
    model[14] = -5;
    if (!expect(build_frustum_planes(view, projection, homogeneous, planes) &&
                    aabb_visible(planes, model, minimum, maximum),
                "off-centre projection"))
    {
        return false;
    }
    bx::mtxOrtho(projection, -3, 3, -2, 2, 0.1F, 100, 0, homogeneous, bx::Handedness::Right);
    if (!expect(build_frustum_planes(view, projection, homogeneous, planes) &&
                    aabb_visible(planes, model, minimum, maximum),
                "orthographic projection"))
    {
        return false;
    }
    model[12] = 3.5F;
    if (!expect(aabb_visible(planes, model, minimum, maximum),
                "orthographic side tangent retained"))
    {
        return false;
    }
    model[12] = 3.6F;
    if (!expect(!aabb_visible(planes, model, minimum, maximum), "orthographic side exclusion"))
    {
        return false;
    }
    // Reflection view is independently extracted, including a negative handedness axis.
    bx::mtxIdentity(view);
    view[0] = -1;
    view[12] = 20;
    model[12] = 20;
    return expect(build_frustum_planes(view, projection, homogeneous, planes) &&
                      aabb_visible(planes, model, minimum, maximum),
                  "mirrored translated view") &&
           expect(!build_frustum_planes(nullptr, projection, homogeneous, planes),
                  "missing matrix rejected");
}
} // namespace

int main()
{
    return tests(false) && tests(true) ? 0 : 1;
}
