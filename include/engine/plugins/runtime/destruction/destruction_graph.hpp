#pragma once

#include "runtime/schema/destruction_generated.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace engine::plugins::destruction
{
struct DestructionGroup
{
    std::uint32_t id = 0;
    std::vector<std::uint32_t> chunks;
    bool anchored = false;
};

// Owns a Blast family; immutable Blast asset storage is shared by cloned candidates.
class DestructionGraph final
{
public:
    DestructionGraph();
    ~DestructionGraph();
    DestructionGraph(DestructionGraph&&) noexcept;
    DestructionGraph& operator=(DestructionGraph&&) noexcept;
    DestructionGraph(const DestructionGraph&) = delete;
    DestructionGraph& operator=(const DestructionGraph&) = delete;

    [[nodiscard]] static bool validate(const schema::DestructionAssetDataT& data,
                                       std::string& error);
    [[nodiscard]] bool initialize(const schema::DestructionAssetDataT& data, std::string& error);
    [[nodiscard]] bool clone(DestructionGraph& output, std::string& error) const;
    // Center is in the original asset coordinate frame of the selected current group.
    [[nodiscard]] bool damage_sphere(std::uint32_t group_id, const schema::Vec3& center,
                                     float radius, float damage, std::string& error);
    // Source chunk IDs; only their live world bonds are fractured. Duplicates are harmless.
    [[nodiscard]] bool release_anchors(std::span<const std::uint32_t> chunk_ids,
                                       std::string& error);
    // Retire complete detached groups using Blast actor deactivation. Failure preserves the graph.
    [[nodiscard]] bool retire_groups(std::span<const std::uint32_t> group_ids,
                                      std::string& error);
    // Asset-local gravity; the caller corrects world rotation and uniform scale.
    [[nodiscard]] bool apply_stress(std::uint32_t group_id, const schema::Vec3& gravity,
                                     float density, float elastic_limit, float fatal_limit,
                                     std::uint32_t iterations, std::uint32_t& broken_bonds,
                                     std::string& error);
    [[nodiscard]] bool restore_health(std::span<const float> health, std::string& error);
    [[nodiscard]] std::vector<float> healths() const;
    [[nodiscard]] const schema::DestructionAssetDataT& data() const noexcept;
    [[nodiscard]] const std::vector<DestructionGroup>& groups() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace engine::plugins::destruction
