#pragma once

#include "runtime/system/system.hpp"

#include <cstdint>
#include <set>

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
};

} // namespace engine::plugins::terrain
