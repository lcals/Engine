#include "runtime/render/mesh_lod_selection.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "LOD selection: %s\n", message);
    }
    return value;
}

bool tests()
{
    using namespace engine::render_detail;
    engine::schema::MeshDataT mesh;
    for (const float threshold : {0.3F, 0.15F, 0.075F})
    {
        auto lod = std::make_unique<engine::schema::MeshLodDataT>();
        lod->screen_height = threshold;
        mesh.lods.push_back(std::move(lod));
    }
    if (!expect(select_mesh_lod(mesh, 0.02F) == 3 && select_mesh_lod(mesh, 0.8F, 3) == 0,
                "large camera moves can cross all levels in either direction") ||
        !expect(select_mesh_lod(mesh, 0.29F, 0) == 0 && select_mesh_lod(mesh, 0.31F, 1) == 1 &&
                    select_mesh_lod(mesh, 0.26F, 0) == 1 && select_mesh_lod(mesh, 0.34F, 1) == 0,
                "hysteresis retains current level around thresholds") ||
        !expect(select_mesh_lod(mesh, std::numeric_limits<float>::infinity(), 3) == 0 &&
                    select_mesh_lod(mesh, std::numeric_limits<float>::quiet_NaN(), 3) == 0,
                "unsafe or nonfinite projection always uses original geometry"))
    {
        return false;
    }
    const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    auto model = identity;
    model[14] = -10;
    // Right-handed perspective with near .1; clip.w = -view.z.
    std::array<float, 16> perspective{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, -1, 0, 0, -.1F, 0};
    const std::array<float, 3> low{-1, -1, -1}, high{1, 1, 1};
    const float height =
        projected_mesh_height(low, high, model.data(), identity.data(), perspective.data(), .1F);
    if (!expect(std::abs(height - 1.0F / 9.0F) < 1e-6F,
                "perspective uses nearest conservative box corners"))
    {
        return false;
    }
    model[12] = 100;
    if (!expect(projected_mesh_height(low, high, model.data(), identity.data(), perspective.data(),
                                      .1F) == height,
                "offscreen placement does not clamp projected size to viewport"))
    {
        return false;
    }
    auto ortho = identity;
    ortho[5] = .2F;
    if (!expect(std::abs(projected_mesh_height(low, high, model.data(), identity.data(),
                                               ortho.data(), .1F) -
                         .2F) < 1e-6F,
                "orthographic cameras use the actual projection scale"))
    {
        return false;
    }
    model[5] = 3;
    if (!expect(std::abs(projected_mesh_height(low, high, model.data(), identity.data(),
                                               ortho.data(), .1F) -
                         .6F) < 1e-6F,
                "nonuniform object scaling affects projected coverage"))
    {
        return false;
    }
    model[14] = -.5F;
    return expect(std::isinf(projected_mesh_height(low, high, model.data(), identity.data(),
                                                   perspective.data(), .1F)),
                  "box crossing near clip forces full-detail geometry");
}
} // namespace

int main()
{
    return tests() ? 0 : 1;
}
