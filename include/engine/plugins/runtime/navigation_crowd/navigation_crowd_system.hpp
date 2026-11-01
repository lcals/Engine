#pragma once

#include "runtime/system/system.hpp"

#include <cstdint>
#include <memory>

namespace engine
{
class WorldSystem;
class NavigationSystem;
} // namespace engine

namespace engine::plugins::navigation_crowd
{

class NavigationCrowdSystem final : public System
{
public:
    // 两个借用须覆盖本系统生命周期；容量是内存边界，不代表性能保证。
    NavigationCrowdSystem(WorldSystem& world, NavigationSystem& navigation,
                          std::uint32_t capacity = 512) noexcept;
    ~NavigationCrowdSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    void release_agents() noexcept;
    struct State;
    WorldSystem& world_;
    NavigationSystem& navigation_;
    const std::uint32_t capacity_;
    std::unique_ptr<State> state_;
};

} // namespace engine::plugins::navigation_crowd
