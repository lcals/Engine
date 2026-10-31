#pragma once

#include "runtime/schema/destruction_state_generated.h"
#include "runtime/system/system.hpp"

#include <memory>
#include <vector>

namespace engine
{
class WorldSystem;
class Node;
class PhysicsSystem;
class Collider;
} // namespace engine

namespace engine::plugins::destruction
{
class DestructionComponent;
class DestructionGraph;

class DestructionSystem final : public System
{
public:
    // Borrowed world and physics outlive this system. Mutations occur at frame boundaries.
    DestructionSystem(WorldSystem& world, PhysicsSystem& physics) noexcept;
    ~DestructionSystem() noexcept override;
    [[nodiscard]] static DestructionSystem* instance() noexcept;
    [[nodiscard]] bool synchronize();
    // Synchronize only this owner, including cleanup after its component was removed.
    [[nodiscard]] bool synchronize(Node& node);
    [[nodiscard]] bool rebuild(DestructionComponent& component, DestructionGraph candidate,
                               bool preserve);
    [[nodiscard]] bool damage(DestructionComponent& component, const schema::Vec3& center,
                              float radius, float damage, float impulse);
    [[nodiscard]] bool refresh_support(DestructionComponent& component, const Collider& support,
                                       float max_gap, float min_coverage);
    [[nodiscard]] bool owns_fragment(const DestructionComponent& component,
                                     std::uint64_t node_id) const noexcept;
    [[nodiscard]] bool reset(DestructionComponent& component);
    [[nodiscard]] bool snapshot(const DestructionComponent& component,
                                schema::DestructionStateDataT& output) const;
    [[nodiscard]] bool restore(DestructionComponent& component,
                               const schema::DestructionStateDataT& state);

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    [[nodiscard]] bool prepare_groups(DestructionComponent& component,
                                      const DestructionGraph& candidate, bool preserve,
                                      std::vector<schema::DestructionGroupStateT>& states);
    [[nodiscard]] bool publish(DestructionComponent& component, DestructionGraph candidate,
                               const std::vector<schema::DestructionGroupStateT>& groups);
    struct State;
    WorldSystem& world_;
    PhysicsSystem& physics_;
    std::unique_ptr<State> state_;
};

} // namespace engine::plugins::destruction
