#include "plugins/editor/destruction/destruction_authoring.hpp"
#include "plugins/runtime/destruction/destruction_graph.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
using namespace engine;
using plugins::destruction::DestructionAuthoring;
using plugins::destruction::DestructionGraph;

bool contract(schema::DestructionFracture method)
{
    schema::DestructionGeneratorDataT settings;
    settings.size = {4, 8, 4};
    settings.slices_x = 2;
    settings.slices_y = 4;
    settings.slices_z = 2;
    settings.sites = 16;
    settings.fracture = method;
    settings.seed = 1729;
    schema::DestructionAssetDataT asset;
    std::string error;
    if (!DestructionAuthoring::generate(settings, asset, error))
    {
        std::fprintf(stderr, "destruction authoring: %s\n", error.c_str());
        return false;
    }
    float volume = 0;
    std::size_t anchored = 0;
    for (const auto& chunk : asset.chunks)
    {
        volume += chunk->volume;
        anchored += chunk->anchored ? 1U : 0U;
        for (const auto& vertex : chunk->mesh->vertices)
        {
            if (vertex.x() < -2.001F || vertex.x() > 2.001F || vertex.y() < -0.001F ||
                vertex.y() > 8.001F || vertex.z() < -2.001F || vertex.z() > 2.001F)
            {
                std::fprintf(stderr, "destruction authoring: fragment left asset-local bounds\n");
                return false;
            }
        }
    }
    if (asset.chunks.size() != 16 || asset.bonds.empty() ||
        (method == schema::DestructionFracture::slices && asset.bonds.size() != 28) ||
        anchored == 0 || anchored == asset.chunks.size() || std::abs(volume - 128.0F) > 0.05F)
    {
        std::fprintf(
            stderr, "destruction authoring: method=%u chunks=%zu bonds=%zu anchors=%zu volume=%f\n",
            static_cast<unsigned>(method), asset.chunks.size(), asset.bonds.size(), anchored,
            volume);
        return false;
    }
    DestructionGraph graph;
    if (!graph.initialize(asset, error) || graph.groups().size() != 1 ||
        !graph.groups()[0].anchored || !graph.damage_sphere(0, {0, 2, 0}, 100, 100, error) ||
        graph.groups().size() != asset.chunks.size())
    {
        std::fprintf(stderr, "destruction authoring graph: groups=%zu %s\n", graph.groups().size(),
                     error.c_str());
        return false;
    }
    for (const auto& group : graph.groups())
    {
        if (group.anchored)
        {
            std::fprintf(stderr, "destruction authoring: broken world bond stayed anchored\n");
            return false;
        }
    }
    return true;
}
} // namespace

int main()
{
    return contract(engine::schema::DestructionFracture::slices) &&
                   contract(engine::schema::DestructionFracture::voronoi)
               ? 0
               : 1;
}
