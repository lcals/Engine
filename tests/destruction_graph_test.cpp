#include "plugins/runtime/destruction/destruction_graph.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
using namespace engine;
using plugins::destruction::DestructionGraph;

bool expect(bool value, const char* message, const std::string& error = {})
{
    if (!value)
    {
        std::fprintf(stderr, "destruction graph: %s (%s)\n", message, error.c_str());
    }
    return value;
}

schema::DestructionAssetDataT chain()
{
    schema::DestructionAssetDataT asset;
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        auto chunk = std::make_unique<schema::DestructionChunkDataT>();
        chunk->centroid = {static_cast<float>(i), 0.5F, 0};
        chunk->mesh = std::make_unique<schema::MeshDataT>();
        chunk->mesh->path = "test/chunk";
        chunk->mesh->vertices = {{static_cast<float>(i), 0, 0, 0, 1, 0},
                                 {static_cast<float>(i) + 0.5F, 0, 0, 0, 1, 0},
                                 {static_cast<float>(i), 1, 0, 0, 1, 0}};
        chunk->mesh->indices = {0, 1, 2};
        chunk->anchored = i == 0;
        asset.chunks.push_back(std::move(chunk));
    }
    for (std::uint32_t i = 0; i < 2; ++i)
    {
        auto bond = std::make_unique<schema::DestructionBondDataT>();
        bond->chunk_a = i;
        bond->chunk_b = i + 1;
        bond->centroid = {static_cast<float>(i) + 0.5F, 0.5F, 0};
        bond->normal = {1, 0, 0};
        asset.bonds.push_back(std::move(bond));
    }
    return asset;
}

bool contract()
{
    std::string error;
    DestructionGraph graph;
    auto asset = chain();
    if (!expect(graph.initialize(asset, error), "initialize", error) ||
        !expect(graph.groups().size() == 1 && graph.groups().front().anchored,
                "one world-anchored group"))
    {
        return false;
    }
    if (!expect(graph.damage_sphere(0, {0.5F, 0.5F, 0}, 0.1F, 0.25F, error), "partial damage",
                error) ||
        !expect(graph.groups().size() == 1 && graph.healths()[0] == 0.75F,
                "partial damage retains connectivity and exact health"))
    {
        return false;
    }
    DestructionGraph candidate;
    if (!expect(graph.clone(candidate, error), "clone", error) ||
        !expect(candidate.damage_sphere(0, {0.5F, 0.5F, 0}, 0.1F, 1, error), "detach B/C", error) ||
        !expect(candidate.groups().size() == 2 && candidate.groups()[0].anchored &&
                    !candidate.groups()[1].anchored && graph.groups().size() == 1 &&
                    graph.healths()[0] == 0.75F && &graph.data() == &candidate.data(),
                "candidate isolates family mutation and shares immutable asset"))
    {
        return false;
    }
    const auto partial_health = graph.healths();
    const auto broken_health = candidate.healths();
    if (!expect(graph.restore_health(broken_health, error), "restore broken graph", error) ||
        !expect(graph.groups().size() == 2 && graph.healths() == broken_health,
                "restoring zero bonds also restores disconnected topology") ||
        !expect(graph.damage_sphere(1, {1.5F, 0.5F, 0}, 0.1F, 1, error),
                "damage surviving group after split", error) ||
        !expect(graph.groups().size() == 3, "continuous fracture yields three groups") ||
        !expect(graph.restore_health(partial_health, error), "restore partial health", error) ||
        !expect(graph.groups().size() == 1 && graph.healths() == partial_health,
                "restore merges connectivity and preserves partial health exactly"))
    {
        return false;
    }
    const auto before = graph.healths();
    auto invalid = before;
    invalid[0] = std::numeric_limits<float>::quiet_NaN();
    if (!expect(!graph.restore_health(invalid, error) && graph.healths() == before,
                "invalid restore leaves the live graph untouched") ||
        !expect(graph.damage_sphere(0, {0, 0, 0}, 0.1F, 2, error), "break only world anchor",
                error) ||
        !expect(graph.groups().size() == 1 && !graph.groups()[0].anchored,
                "losing world connectivity makes the complete building dynamic"))
    {
        return false;
    }
    return true;
}

bool anchor_contract()
{
    std::string error;
    auto asset = chain();
    asset.chunks[2]->anchored = true;
    DestructionGraph graph;
    if (!expect(graph.initialize(asset, error) &&
                    graph.damage_sphere(0, {0.5F, 0.5F, 0}, 0.1F, 0.25F, error) &&
                    graph.damage_sphere(0, {2, 0, 0}, 0.1F, 0.25F, error),
                "prepare independent partial internal and world bond damage", error))
    {
        return false;
    }
    const auto before = graph.healths();
    const std::uint32_t invalid[] = {0, 3};
    const std::uint32_t first[] = {0, 0, 1};
    if (!expect(before == std::vector<float>{0.75F, 1, 1, 0.75F} &&
                    !graph.release_anchors(invalid, error) && graph.healths() == before,
                "validate every chunk before releasing any anchor") ||
        !expect(graph.release_anchors(first, error) &&
                    graph.healths() == std::vector<float>{0.75F, 1, 0, 0.75F} &&
                    graph.groups().size() == 1 && graph.groups().front().anchored,
                "release only the selected world bond, deduplicating and ignoring nonanchors",
                error))
    {
        return false;
    }
    const auto once = graph.healths();
    DestructionGraph candidate;
    const std::uint32_t last[] = {2};
    if (!expect(graph.release_anchors(first, error) && graph.healths() == once &&
                    graph.clone(candidate, error) && candidate.release_anchors(last, error) &&
                    candidate.healths() == std::vector<float>{0.75F, 1, 0, 0} &&
                    candidate.groups().size() == 1 && !candidate.groups().front().anchored &&
                    graph.healths() == once && graph.groups().front().anchored,
                "anchor release is repeatable and isolated from the live graph", error))
    {
        return false;
    }
    // Exercise several selected anchors after Blast has already split the structure.
    auto detached = before;
    detached[0] = 0;
    detached[1] = 0;
    const std::uint32_t both[] = {2, 0};
    if (!expect(graph.restore_health(detached, error) && graph.release_anchors(both, error) &&
                    graph.healths() == std::vector<float>{0, 0, 0, 0} && graph.groups().size() == 3,
                "release anchors by chunk identity after prior actor splits", error))
    {
        return false;
    }
    for (const auto& group : graph.groups())
    {
        if (!expect(!group.anchored, "no selected world anchor survives"))
        {
            return false;
        }
    }
    return true;
}
} // namespace

int main()
{
    return contract() && anchor_contract() ? 0 : 1;
}
