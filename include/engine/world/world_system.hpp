#pragma once

#include "system.hpp"

struct ecs_world_t;

namespace engine
{

class WorldSystem final : public System
{
public:
    WorldSystem() noexcept;
    ~WorldSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    ecs_world_t* world_ = nullptr;
};

} // namespace engine
