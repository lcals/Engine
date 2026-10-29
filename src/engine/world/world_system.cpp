#include "world_system.hpp"

#include <flecs.h>

#include <cstdio>
#include <limits>

namespace engine
{

WorldSystem::WorldSystem() noexcept = default;

WorldSystem::~WorldSystem() noexcept
{
    shutdown();
}

bool WorldSystem::on_initialize(const EngineConfig&)
{
    world_ = ecs_init();
    return world_ != nullptr;
}

bool WorldSystem::on_tick(double delta_time)
{
    if (delta_time > static_cast<double>(std::numeric_limits<ecs_ftime_t>::max()))
    {
        return false;
    }
    return world_ != nullptr && ecs_progress(world_, static_cast<ecs_ftime_t>(delta_time));
}

void WorldSystem::on_shutdown() noexcept
{
    if (world_ != nullptr)
    {
        if (ecs_fini(world_) != 0)
        {
            std::fputs("Cannot finish the ECS world cleanly\n", stderr);
        }
        world_ = nullptr;
    }
}

} // namespace engine
