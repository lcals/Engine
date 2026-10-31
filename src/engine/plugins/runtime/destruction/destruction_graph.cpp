#include "plugins/runtime/destruction/destruction_graph.hpp"

#include "runtime/utils/mesh_utils.hpp"

#include <NvBlast.h>
#include <NvBlastExtDamageShaders.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <set>
#include <utility>

namespace engine::plugins::destruction
{
namespace
{
constexpr std::uint32_t invalid_index = std::numeric_limits<std::uint32_t>::max();
constexpr std::size_t max_chunks = 256;
constexpr std::size_t max_bonds = 8192;

struct AlignedDelete
{
    void operator()(std::byte* memory) const noexcept
    {
        ::operator delete(memory, std::align_val_t{16});
    }
};
using Memory = std::unique_ptr<std::byte, AlignedDelete>;

Memory allocate(std::size_t size)
{
    return Memory(
        static_cast<std::byte*>(::operator new(size, std::align_val_t{16}, std::nothrow)));
}

bool finite(const schema::Vec3& value)
{
    return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.z()) &&
           std::abs(value.x()) <= std::numeric_limits<float>::max() &&
           std::abs(value.y()) <= std::numeric_limits<float>::max() &&
           std::abs(value.z()) <= std::numeric_limits<float>::max();
}

struct SharedAsset
{
    schema::DestructionAssetDataT data;
    Memory memory;
    NvBlastAsset* asset = nullptr;
    std::vector<float> initial_health;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> nodes;
};
} // namespace

struct DestructionGraph::Impl
{
    std::shared_ptr<SharedAsset> asset;
    Memory memory;
    NvBlastFamily* family = nullptr;
    std::vector<DestructionGroup> groups;

    bool refresh(std::string& error)
    {
        std::vector<NvBlastActor*> actors(NvBlastFamilyGetActorCount(family, nullptr));
        if (NvBlastFamilyGetActors(actors.data(), static_cast<std::uint32_t>(actors.size()), family,
                                   nullptr) != actors.size())
        {
            error = "Blast could not enumerate the current actors";
            return false;
        }
        groups.clear();
        for (const auto* actor : actors)
        {
            DestructionGroup group;
            group.chunks.resize(NvBlastActorGetVisibleChunkCount(actor, nullptr));
            const auto count = NvBlastActorGetVisibleChunkIndices(
                group.chunks.data(), static_cast<std::uint32_t>(group.chunks.size()), actor,
                nullptr);
            if (count != group.chunks.size() || count == 0)
            {
                error = "Blast returned an empty or incomplete fragment group";
                return false;
            }
            std::sort(group.chunks.begin(), group.chunks.end());
            group.id = group.chunks.front();
            group.anchored = NvBlastActorHasExternalBonds(actor, nullptr);
            groups.push_back(std::move(group));
        }
        std::sort(groups.begin(), groups.end(),
                  [](const auto& a, const auto& b) { return a.id < b.id; });
        return true;
    }

    bool split(NvBlastActor* actor, std::string& error)
    {
        const auto size = NvBlastActorGetRequiredScratchForSplit(actor, nullptr);
        auto scratch = allocate(size);
        if (!scratch)
        {
            error = "Cannot allocate Blast split scratch";
            return false;
        }
        std::vector<NvBlastActor*> actors(NvBlastActorGetMaxActorCountForSplit(actor, nullptr));
        NvBlastActorSplitEvent event{nullptr, actors.data()};
        NvBlastActorSplit(&event, actor, static_cast<std::uint32_t>(actors.size()), scratch.get(),
                          nullptr, nullptr);
        return refresh(error);
    }

    bool reset(std::span<const float> health, std::string& error)
    {
        memory = allocate(NvBlastAssetGetFamilyMemorySize(asset->asset, nullptr));
        if (!memory)
        {
            error = "Cannot allocate Blast family";
            return false;
        }
        family = NvBlastAssetCreateFamily(memory.get(), asset->asset, nullptr);
        if (!family)
        {
            error = "Cannot create Blast family";
            return false;
        }
        auto scratch =
            allocate(NvBlastFamilyGetRequiredScratchForCreateFirstActor(family, nullptr));
        if (!scratch)
        {
            error = "Cannot allocate Blast actor scratch";
            return false;
        }
        const auto* bonds = NvBlastAssetGetBonds(asset->asset, nullptr);
        std::vector<float> ordered_health(health.size());
        std::vector<NvBlastBondFractureData> broken;
        for (std::size_t i = 0; i < health.size(); ++i)
        {
            const auto stable = bonds[i].userData;
            ordered_health[i] = health[stable] > 0 ? health[stable] : asset->initial_health[stable];
            if (health[stable] <= 0)
            {
                const auto [a, b] = asset->nodes[stable];
                broken.push_back({0, a, b, ordered_health[i]});
            }
        }
        NvBlastActorDesc description{};
        description.initialBondHealths = ordered_health.data();
        description.uniformInitialLowerSupportChunkHealth = std::numeric_limits<float>::max();
        auto* actor = NvBlastFamilyCreateFirstActor(family, &description, scratch.get(), nullptr);
        if (!actor)
        {
            error = "Cannot create Blast actor";
            return false;
        }
        // Zero initial health alone does not set edgeRemoved in Blast. Reapply explicit fractures.
        if (!broken.empty())
        {
            NvBlastFractureBuffers commands{static_cast<std::uint32_t>(broken.size()), 0,
                                            broken.data(), nullptr};
            NvBlastActorApplyFracture(nullptr, actor, &commands, nullptr, nullptr);
        }
        return split(actor, error);
    }
};

DestructionGraph::DestructionGraph() = default;
DestructionGraph::~DestructionGraph() = default;
DestructionGraph::DestructionGraph(DestructionGraph&&) noexcept = default;
DestructionGraph& DestructionGraph::operator=(DestructionGraph&&) noexcept = default;

bool DestructionGraph::validate(const schema::DestructionAssetDataT& data, std::string& error)
{
    error.clear();
    if (data.schema_version != 1 || data.chunks.empty() || data.chunks.size() > max_chunks ||
        data.bonds.size() > max_bonds)
    {
        error = "Destruction asset requires 1..256 fragments and at most 8192 bonds";
        return false;
    }
    for (const auto& chunk : data.chunks)
    {
        if (!chunk || !chunk->mesh || !MeshUtils::validate(*chunk->mesh) ||
            !finite(chunk->centroid) || !std::isfinite(chunk->volume) || chunk->volume <= 0 ||
            !std::isfinite(chunk->anchor_health) || chunk->anchor_health <= 0 ||
            chunk->anchor_health > 1e6F)
        {
            error = "Destruction fragment mesh, volume, centroid or anchor health is invalid";
            return false;
        }
    }
    std::set<std::pair<std::uint32_t, std::uint32_t>> pairs;
    for (const auto& bond : data.bonds)
    {
        if (!bond || bond->chunk_a >= data.chunks.size() || bond->chunk_b >= data.chunks.size() ||
            bond->chunk_a == bond->chunk_b || !finite(bond->centroid) || !finite(bond->normal) ||
            !std::isfinite(bond->health) || bond->health <= 0 || bond->health > 1e6F ||
            !std::isfinite(bond->area) || bond->area <= 0)
        {
            error = "Destruction bond references, geometry or health are invalid";
            return false;
        }
        if (!pairs
                 .emplace(std::min(bond->chunk_a, bond->chunk_b),
                          std::max(bond->chunk_a, bond->chunk_b))
                 .second)
        {
            error = "Destruction asset contains duplicate bonds";
            return false;
        }
    }
    return true;
}

bool DestructionGraph::initialize(const schema::DestructionAssetDataT& data, std::string& error)
{
    if (!validate(data, error))
    {
        return false;
    }
    auto candidate = std::make_unique<Impl>();
    candidate->asset = std::make_shared<SharedAsset>();
    auto& shared = *candidate->asset;
    shared.data = data;
    std::vector<NvBlastChunkDesc> chunks;
    std::vector<NvBlastBondDesc> bonds;
    for (std::uint32_t i = 0; i < data.chunks.size(); ++i)
    {
        const auto& chunk = *data.chunks[i];
        chunks.push_back(
            {{static_cast<float>(chunk.centroid.x()), static_cast<float>(chunk.centroid.y()),
              static_cast<float>(chunk.centroid.z())},
             chunk.volume,
             invalid_index,
             NvBlastChunkDesc::SupportFlag,
             i});
    }
    for (const auto& bond : data.bonds)
    {
        const auto id = static_cast<std::uint32_t>(bonds.size());
        bonds.push_back(
            {{{static_cast<float>(bond->normal.x()), static_cast<float>(bond->normal.y()),
               static_cast<float>(bond->normal.z())},
              bond->area,
              {static_cast<float>(bond->centroid.x()), static_cast<float>(bond->centroid.y()),
               static_cast<float>(bond->centroid.z())},
              id},
             {bond->chunk_a, bond->chunk_b}});
        shared.initial_health.push_back(bond->health);
    }
    for (std::uint32_t i = 0; i < data.chunks.size(); ++i)
    {
        const auto& chunk = *data.chunks[i];
        if (chunk.anchored)
        {
            const auto id = static_cast<std::uint32_t>(bonds.size());
            float bottom = chunk.mesh->vertices.front().y();
            for (const auto& vertex : chunk.mesh->vertices)
            {
                bottom = std::min(bottom, vertex.y());
            }
            bonds.push_back({{{0, -1, 0},
                              1,
                              {static_cast<float>(chunk.centroid.x()), bottom,
                               static_cast<float>(chunk.centroid.z())},
                              id},
                             {i, invalid_index}});
            shared.initial_health.push_back(chunk.anchor_health);
        }
    }
    const NvBlastAssetDesc description{static_cast<std::uint32_t>(chunks.size()), chunks.data(),
                                       static_cast<std::uint32_t>(bonds.size()), bonds.data()};
    shared.memory = allocate(NvBlastGetAssetMemorySize(&description, nullptr));
    auto scratch = allocate(NvBlastGetRequiredScratchForCreateAsset(&description, nullptr));
    if (!shared.memory || !scratch)
    {
        error = "Cannot allocate Blast asset";
        return false;
    }
    shared.asset = NvBlastCreateAsset(shared.memory.get(), &description, scratch.get(), nullptr);
    if (!shared.asset || NvBlastAssetGetBondCount(shared.asset, nullptr) != bonds.size())
    {
        error = "Blast rejected the fragment graph";
        return false;
    }
    const auto graph = NvBlastAssetGetSupportGraph(shared.asset, nullptr);
    const auto* asset_bonds = NvBlastAssetGetBonds(shared.asset, nullptr);
    shared.nodes.resize(bonds.size());
    for (std::uint32_t node = 0; node < graph.nodeCount; ++node)
    {
        for (auto edge = graph.adjacencyPartition[node]; edge < graph.adjacencyPartition[node + 1];
             ++edge)
        {
            const auto other = graph.adjacentNodeIndices[edge];
            if (node < other)
            {
                shared.nodes[asset_bonds[graph.adjacentBondIndices[edge]].userData] = {node, other};
            }
        }
    }
    if (!candidate->reset(shared.initial_health, error))
    {
        return false;
    }
    impl_ = std::move(candidate);
    return true;
}

bool DestructionGraph::clone(DestructionGraph& output, std::string& error) const
{
    error.clear();
    if (!impl_)
    {
        error = "Destruction graph is not initialized";
        return false;
    }
    auto candidate = std::make_unique<Impl>();
    candidate->asset = impl_->asset;
    const auto size = NvBlastFamilyGetSize(impl_->family, nullptr);
    candidate->memory = allocate(size);
    if (!candidate->memory)
    {
        error = "Cannot clone Blast family";
        return false;
    }
    std::memcpy(candidate->memory.get(), impl_->memory.get(), size);
    candidate->family = reinterpret_cast<NvBlastFamily*>(candidate->memory.get());
    NvBlastFamilySetAsset(candidate->family, candidate->asset->asset, nullptr);
    if (!candidate->refresh(error))
    {
        return false;
    }
    output.impl_ = std::move(candidate);
    return true;
}

bool DestructionGraph::damage_sphere(std::uint32_t group_id, const schema::Vec3& center,
                                     float radius, float damage, std::string& error)
{
    error.clear();
    if (!impl_ || !finite(center) || !std::isfinite(radius) || radius <= 0 ||
        !std::isfinite(damage) || damage < 0)
    {
        error = "Destruction sphere damage parameters are invalid";
        return false;
    }
    const auto group = std::find_if(impl_->groups.begin(), impl_->groups.end(),
                                    [group_id](const auto& value) { return value.id == group_id; });
    if (group == impl_->groups.end())
    {
        error = "Destruction group no longer exists";
        return false;
    }
    if (damage == 0)
    {
        return true;
    }
    auto* actor = NvBlastFamilyGetChunkActor(impl_->family, group_id, nullptr);
    std::vector<NvBlastBondFractureData> fractures(impl_->asset->initial_health.size());
    NvBlastFractureBuffers commands{static_cast<std::uint32_t>(fractures.size()), 0,
                                    fractures.data(), nullptr};
    const NvBlastDamageProgram program{NvBlastExtFalloffGraphShader, nullptr};
    const NvBlastExtRadialDamageDesc description{damage,
                                                 {static_cast<float>(center.x()),
                                                  static_cast<float>(center.y()),
                                                  static_cast<float>(center.z())},
                                                 0,
                                                 radius};
    const NvBlastExtProgramParams params(&description);
    NvBlastActorGenerateFracture(&commands, actor, program, &params, nullptr, nullptr);
    NvBlastActorApplyFracture(nullptr, actor, &commands, nullptr, nullptr);
    return impl_->split(actor, error);
}

bool DestructionGraph::restore_health(std::span<const float> health, std::string& error)
{
    error.clear();
    if (!impl_ || health.size() != impl_->asset->initial_health.size())
    {
        error = "Destruction health snapshot does not match the asset";
        return false;
    }
    for (std::size_t i = 0; i < health.size(); ++i)
    {
        if (!std::isfinite(health[i]) || health[i] < 0 ||
            health[i] > impl_->asset->initial_health[i])
        {
            error = "Destruction snapshot contains invalid bond health";
            return false;
        }
    }
    auto candidate = std::make_unique<Impl>();
    candidate->asset = impl_->asset;
    if (!candidate->reset(health, error))
    {
        return false;
    }
    impl_ = std::move(candidate);
    return true;
}

std::vector<float> DestructionGraph::healths() const
{
    if (!impl_ || impl_->groups.empty())
    {
        return {};
    }
    const auto* actor =
        NvBlastFamilyGetChunkActor(impl_->family, impl_->groups.front().id, nullptr);
    const auto* health = NvBlastActorGetBondHealths(actor, nullptr);
    const auto* bonds = NvBlastAssetGetBonds(impl_->asset->asset, nullptr);
    std::vector<float> output(impl_->asset->initial_health.size());
    for (std::size_t i = 0; i < output.size(); ++i)
    {
        output[bonds[i].userData] = std::max(0.0F, health[i]);
    }
    return output;
}

const schema::DestructionAssetDataT& DestructionGraph::data() const noexcept
{
    static const schema::DestructionAssetDataT empty;
    return impl_ ? impl_->asset->data : empty;
}

const std::vector<DestructionGroup>& DestructionGraph::groups() const noexcept
{
    static const std::vector<DestructionGroup> empty;
    return impl_ ? impl_->groups : empty;
}
} // namespace engine::plugins::destruction
