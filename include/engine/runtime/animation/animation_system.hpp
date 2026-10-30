#pragma once

#include "runtime/system/system.hpp"

#include <memory>

namespace engine
{

class WorldSystem;

class AnimationSystem final : public System
{
public:
    // 可空 world 由调用方拥有，存活期须覆盖本系统。
    explicit AnimationSystem(WorldSystem* world = nullptr) noexcept;
    ~AnimationSystem() noexcept override;
    void clear() noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    struct State;
    WorldSystem* world_ = nullptr;
    std::unique_ptr<State> state_;
};

} // namespace engine
