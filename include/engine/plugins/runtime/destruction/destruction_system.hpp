#pragma once

#include "runtime/schema/destruction_state_generated.h"
#include "runtime/system/system.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace engine
{
class WorldSystem;
class Node;
class PhysicsSystem;
class Collider;
class TaskSystem;
} // namespace engine

namespace engine::plugins::destruction
{
class DestructionComponent;
class DestructionGraph;

class DestructionSystem final : public System
{
public:
    // Borrowed world and physics outlive this system. Mutations occur at frame boundaries.
    DestructionSystem(WorldSystem& world, PhysicsSystem& physics, TaskSystem* tasks = nullptr,
                      std::size_t max_pending_requests = 64,
                      std::size_t max_commits_per_tick = 1) noexcept;
    ~DestructionSystem() noexcept override;
    [[nodiscard]] static DestructionSystem* instance() noexcept;
    [[nodiscard]] bool synchronize();
    // Synchronize only this owner, including cleanup after its component was removed.
    [[nodiscard]] bool synchronize(Node node);
    [[nodiscard]] bool rebuild(DestructionComponent component, DestructionGraph candidate,
                               bool preserve);
    [[nodiscard]] bool damage(DestructionComponent component, const schema::Vec3& center,
                              float radius, float damage, float impulse);
    // true 仅表示排队；完成或取消委托由主线程 synchronize/tick 派发，关闭丢弃委托。
    [[nodiscard]] bool request_damage(DestructionComponent component, const schema::Vec3& center,
                                      float radius, float damage, float impulse,
                                      std::function<void(bool)> completed);
    [[nodiscard]] bool refresh_support(DestructionComponent component, Collider support,
                                       float max_gap, float min_coverage);
    [[nodiscard]] bool apply_stress(DestructionComponent component, const schema::Vec3& gravity,
                                    float elastic_limit, float fatal_limit,
                                    std::uint32_t iterations, std::uint32_t& broken_bonds);
    [[nodiscard]] bool owns_fragment(DestructionComponent component,
                                     std::uint64_t node_id) const noexcept;
    [[nodiscard]] bool retire_fragments(DestructionComponent component, std::uint32_t max_count,
                                        double max_age_seconds, std::uint32_t& retired_count);
    [[nodiscard]] bool reset(DestructionComponent component);
    [[nodiscard]] bool snapshot(DestructionComponent component,
                                schema::DestructionStateDataT& output) const;
    [[nodiscard]] bool restore(DestructionComponent component,
                               const schema::DestructionStateDataT& state);

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    struct PreparedGroup;
    [[nodiscard]] bool prepare_groups(DestructionComponent component,
                                      const DestructionGraph& candidate, bool preserve,
                                      std::vector<schema::DestructionGroupStateT>& states);
    [[nodiscard]] bool publish(DestructionComponent component, DestructionGraph candidate,
                               const std::vector<schema::DestructionGroupStateT>& groups,
                               bool reuse_groups,
                               const std::vector<PreparedGroup>* prepared = nullptr);
    void process_requests();
    struct State;
    WorldSystem& world_;
    PhysicsSystem& physics_;
    TaskSystem* tasks_ = nullptr;
    std::size_t max_pending_requests_ = 64;
    std::size_t max_commits_per_tick_ = 1;
    std::unique_ptr<State> state_;
};

} // namespace engine::plugins::destruction
