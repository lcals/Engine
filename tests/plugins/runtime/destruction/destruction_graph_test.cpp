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

bool retirement_contract()
{
    DestructionGraph graph;
    DestructionGraph copy;
    const auto asset = chain();
    std::string error;
    const std::uint32_t anchored[] = {0};
    const std::uint32_t last[] = {2};
    const std::uint32_t duplicate[] = {2, 2};
    const std::uint32_t invalid[] = {2, 99};
    if (!expect(graph.initialize(asset, error) &&
                    graph.damage_sphere(0, {1.5, 0.5, 0}, 0.1F, 1, error),
                "prepare one detached and one anchored actor", error))
    {
        return false;
    }
    const auto before = graph.healths();
    if (!expect(!graph.retire_groups(anchored, error) && !graph.retire_groups(duplicate, error) &&
                    !graph.retire_groups(invalid, error) && graph.groups().size() == 2 &&
                    graph.healths() == before && graph.retire_groups(last, error) &&
                    graph.groups().size() == 1 && graph.groups().front().chunks.size() == 2 &&
                    graph.healths() == before,
                "invalid retirement is atomic; official deactivation keeps bond health", error))
    {
        return false;
    }
    std::uint32_t broken = 99;
    if (!expect(graph.clone(copy, error) && copy.restore_health(before, error) &&
                    copy.apply_stress(0, {0, -9.81, 0}, 100, 1e8F, 2e8F, 500, broken, error) &&
                    copy.groups().size() == 1 && copy.groups().front().chunks.size() == 2 &&
                    broken == 0 && copy.healths() == before,
                "cloning, health restoration and stress do not revive retired chunks", error))
    {
        return false;
    }
    auto reconnect = before;
    reconnect[1] = 1;
    if (!expect(!graph.restore_health(reconnect, error) && graph.healths() == before &&
                    graph.release_anchors(anchored, error) &&
                    graph.retire_groups(anchored, error) && graph.groups().empty(),
                "retirement cannot reconnect to a live actor; all detached actors may retire",
                error))
    {
        return false;
    }
    const auto empty_health = graph.healths();
    return expect(empty_health.size() == before.size() && graph.clone(copy, error) &&
                      copy.groups().empty() && copy.healths() == empty_health &&
                      copy.restore_health(empty_health, error) && copy.groups().empty() &&
                      copy.healths() == empty_health && graph.initialize(asset, error) &&
                      graph.groups().size() == 1 && graph.groups().front().chunks.size() == 3,
                  "empty families keep health and clone/restore correctly; initialize resets",
                  error);
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
bool stress_contract()
{
    std::string error;
    DestructionGraph graph;
    const auto asset = chain();
    std::uint32_t broken = 99;
    if (!expect(graph.initialize(asset, error), "initialize stress cantilever", error))
    {
        return false;
    }
    const auto intact = graph.healths();
    if (!expect(graph.apply_stress(0, {0, -9.81, 0}, 100, 1e8F, 2e8F, 500, broken, error) &&
                    broken == 0 && graph.healths() == intact && graph.groups().size() == 1,
                "high-strength cantilever carries gravity without damage", error) ||
        !expect(!graph.apply_stress(0, {0, -9.81, 0}, 100, 20, 10, 500, broken, error) &&
                    broken == 0 && graph.healths() == intact,
                "invalid stress limits do not mutate the family", error) ||
        !expect(!graph.apply_stress(0, {0, -9.81, 0}, 100, 10, 20, 0, broken, error) &&
                    broken == 0 && graph.healths() == intact,
                "invalid iteration budget does not mutate the family", error))
    {
        return false;
    }
    if (!expect(graph.apply_stress(0, {0, -9.81, 0}, 100, 1, 1e9F, 500, broken, error) &&
                    broken == 0 && graph.healths() != intact && graph.groups().size() == 1,
                "sub-fatal official stress preserves partial health for later calls", error) ||
        !expect(graph.restore_health(intact, error), "restore partial stress state", error))
    {
        return false;
    }
    if (!expect(graph.apply_stress(0, {0, -9.81, 0}, 100, 10, 20, 500, broken, error) &&
                    broken > 0 && graph.groups().size() > 1,
                "official gravity stress fractures an under-strength cantilever", error))
    {
        return false;
    }
    const auto fractured = graph.healths();
    for (const auto& group : graph.groups())
    {
        if (!group.anchored &&
            !expect(
                graph.apply_stress(group.id, {0, -9.81, 0}, 100, 0.1F, 0.2F, 500, broken, error) &&
                    broken == 0 && graph.healths() == fractured,
                "free-falling group is not damaged by uniform gravity", error))
        {
            return false;
        }
    }
    // Gameplay health is independent of physical area: changing health alone must not
    // change the stress fracture decision or the remaining fraction of material.
    auto stronger_health = asset;
    for (auto& bond : stronger_health.bonds)
    {
        bond->health = 7;
    }
    for (auto& chunk : stronger_health.chunks)
    {
        chunk->anchor_health = 7;
    }
    DestructionGraph baseline;
    DestructionGraph alternate;
    if (!expect(baseline.initialize(asset, error) && alternate.initialize(stronger_health, error) &&
                    baseline.apply_stress(0, {0, -9.81, 0}, 100, 1, 1e6F, 500, broken, error) &&
                    alternate.apply_stress(0, {0, -9.81, 0}, 100, 1, 1e6F, 500, broken, error),
                "solve equivalent materials with different gameplay health", error))
    {
        return false;
    }
    const auto baseline_health = baseline.healths();
    const auto alternate_health = alternate.healths();
    for (std::size_t i = 0; i < baseline_health.size(); ++i)
    {
        if (!expect(std::abs(baseline_health[i] - alternate_health[i] / 7) < 1e-6F,
                    "Pa stress damage is independent of project damage-unit normalization"))
        {
            return false;
        }
    }
    // Moving only the asset coordinate origin must not change gravity stress. In this
    // translated asset the anchored chunk centroid is exactly the virtual world origin.
    auto recentered = asset;
    for (auto& chunk : recentered.chunks)
    {
        chunk->centroid = {chunk->centroid.x(), chunk->centroid.y() - 0.5, chunk->centroid.z()};
        for (auto& vertex : chunk->mesh->vertices)
        {
            vertex = {vertex.x(),  vertex.y() - 0.5F, vertex.z(),
                      vertex.nx(), vertex.ny(),       vertex.nz()};
        }
    }
    for (auto& bond : recentered.bonds)
    {
        bond->centroid = {bond->centroid.x(), bond->centroid.y() - 0.5, bond->centroid.z()};
    }
    if (!expect(alternate.initialize(recentered, error) &&
                    alternate.apply_stress(0, {0, -9.81, 0}, 100, 1, 1e6F, 500, broken, error),
                "stress supports an anchor centroid at the asset origin", error))
    {
        return false;
    }
    const auto recentered_health = alternate.healths();
    for (std::size_t i = 0; i < baseline_health.size(); ++i)
    {
        if (!expect(std::abs(baseline_health[i] - recentered_health[i]) < 1e-6F,
                    "changing asset origin preserves every physical stress damage fraction"))
        {
            return false;
        }
    }
    auto wider_bonds = asset;
    for (auto& bond : wider_bonds.bonds)
    {
        bond->area *= 4;
    }
    if (!expect(alternate.initialize(wider_bonds, error) &&
                    alternate.apply_stress(0, {0, -9.81, 0}, 100, 1, 1e6F, 500, broken, error) &&
                    alternate.healths()[0] > baseline_health[0],
                "larger physical bond area lowers stress with unchanged gameplay health", error))
    {
        return false;
    }
    const std::uint32_t anchor[] = {0};
    if (!expect(graph.restore_health(intact, error) && graph.release_anchors(anchor, error),
                "reset and explicitly remove the foundation", error))
    {
        return false;
    }
    const auto detached = graph.healths();
    return expect(graph.apply_stress(0, {0, -9.81, 0}, 100, 0.1F, 0.2F, 500, broken, error) &&
                      broken == 0 && graph.healths() == detached,
                  "stress honors anchors already released by foundation changes", error);
}
} // namespace

int main()
{
    return contract() && anchor_contract() && stress_contract() && retirement_contract() ? 0 : 1;
}
