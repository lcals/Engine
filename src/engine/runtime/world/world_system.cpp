#include "runtime/world/world_system.hpp"

#include "runtime/scene/scene.hpp"

#include <flecs.h>

#include <cstdio>
#include <limits>
#include <new>

namespace engine
{

WorldSystem::WorldSystem() noexcept = default;

WorldSystem::~WorldSystem() noexcept
{
    shutdown();
}

Scene* WorldSystem::create_scene(std::string_view name)
{
    if (!is_initialized() || world_ == nullptr || scene_ != nullptr ||
        scene_sequence_ == std::numeric_limits<std::uint64_t>::max())
    {
        return nullptr;
    }
    scene_.reset(new (std::nothrow) Scene(*world_, scene_sequence_ + 1, name));
    if (scene_ != nullptr)
    {
        ++scene_sequence_;
    }
    return scene_.get();
}

void WorldSystem::destroy_scene() noexcept
{
    scene_.reset();
}

Scene* WorldSystem::scene() noexcept
{
    return scene_.get();
}

const Scene* WorldSystem::scene() const noexcept
{
    return scene_.get();
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
    destroy_scene();
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
