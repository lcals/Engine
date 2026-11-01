#pragma once

#include "runtime/system/system.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace engine
{
class WorldSystem;
class PhysicsSystem;
} // namespace engine

namespace engine::plugins::terrain
{
class TerrainComponent;
class TerrainVolume;

class TerrainSystem final : public System
{
public:
    // Borrowed world/physics must outlive this system. Called synchronously at frame boundaries.
    TerrainSystem(WorldSystem& world, PhysicsSystem& physics) noexcept;
    ~TerrainSystem() noexcept override;
    [[nodiscard]] static TerrainSystem* instance() noexcept;
    [[nodiscard]] bool commit(TerrainComponent& component, TerrainVolume candidate);
    [[nodiscard]] bool collapse_detached(TerrainComponent& component, std::uint32_t min_samples,
                                         std::uint32_t max_fragments, std::uint32_t fragment_cells,
                                         float density);
    void clear_fragments(TerrainComponent& component) noexcept;
    [[nodiscard]] std::size_t fragment_count(const TerrainComponent& component) const noexcept;
    [[nodiscard]] bool synchronize();

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    WorldSystem& world_;
    PhysicsSystem& physics_;
    std::uint64_t scene_id_ = 0;
    std::set<std::uint64_t> generated_;
    std::map<std::uint64_t, std::vector<std::uint64_t>> fragments_;
};

} // namespace engine::plugins::terrain
