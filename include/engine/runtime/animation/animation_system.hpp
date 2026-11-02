#pragma once

#include "runtime/system/system.hpp"

#include <cstddef>
#include <memory>

namespace engine
{

class WorldSystem;
class TaskSystem;

struct AnimationConfig
{
    std::size_t min_parallel_count = 8;
    std::size_t batch_size = 16;
};

class AnimationSystem final : public System
{
public:
    // 可空 world/tasks 由调用方拥有，存活期须覆盖本系统；tasks 须先初始化。
    explicit AnimationSystem(WorldSystem* world = nullptr, TaskSystem* tasks = nullptr,
                             const AnimationConfig& config = {}) noexcept;
    ~AnimationSystem() noexcept override;
    void clear() noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    struct State;
    WorldSystem* world_ = nullptr;
    TaskSystem* tasks_ = nullptr;
    AnimationConfig config_;
    std::unique_ptr<State> state_;
};

} // namespace engine
