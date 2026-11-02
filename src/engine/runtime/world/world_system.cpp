#include "runtime/world/world_system.hpp"

#include "runtime/log/log_system.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/scene.hpp"

#include <flecs.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <new>
#include <string>

namespace engine
{

WorldSystem::WorldSystem(const SceneRestoreConfig& config) noexcept : restore_config_(config)
{
}

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
    scene_.reset(new (std::nothrow) Scene(*world_, scene_sequence_ + 1, name, components_));
    if (scene_ != nullptr)
    {
        ++scene_sequence_;
        restoring_.reset();
        paused_ = false;
    }
    return scene_.get();
}

Scene* WorldSystem::load_scene(std::string_view path)
{
    if (!is_initialized() || world_ == nullptr ||
        scene_sequence_ == std::numeric_limits<std::uint64_t>::max())
    {
        return nullptr;
    }
    auto* resources = ResourceSystem::instance();
    Guid guid;
    ResourcePtr resource;
    if (resources == nullptr || !ResourceSystem::path_to_guid(path, guid))
    {
        LogSystem::write(LogLevel::error, "Scene path is unavailable: " + std::string(path));
        return nullptr;
    }
    if (!resources->loader().load(guid, resource))
    {
        LogSystem::write(LogLevel::error,
                         "Cannot load scene " + std::string(path) + ": " +
                             resources->loader().last_error());
        return nullptr;
    }
    const auto* data = resource->scene();
    if (data == nullptr || !data->info)
    {
        return nullptr;
    }
    std::unique_ptr<Scene> candidate(
        new (std::nothrow) Scene(*world_, scene_sequence_ + 1, data->info->name, components_));
    if (!candidate || !candidate->restore_data(*data))
    {
        LogSystem::write(LogLevel::error, "Cannot restore scene: " + std::string(path));
        return nullptr;
    }
    candidate->resource_ = std::move(resource);
    restoring_.reset();
    scene_ = std::move(candidate);
    ++scene_sequence_;
    paused_ = false;
    return scene_.get();
}

std::uint64_t WorldSystem::begin_restore_scene(std::shared_ptr<const Resource> resource)
{
    if (!is_initialized() || world_ == nullptr || restoring_ || !resource ||
        !resource->scene() || !resource->scene()->info || resource->scene()->schema_version != 1 ||
        scene_sequence_ == std::numeric_limits<std::uint64_t>::max())
    {
        return 0;
    }
    auto* resources = ResourceSystem::instance();
    if (resources == nullptr || resources->generation() != resource->generation())
    {
        return 0;
    }
    const auto& data = *resource->scene();
    restoring_.reset(new (std::nothrow) Scene(*world_, ++scene_sequence_, data.info->name, components_));
    if (!restoring_)
    {
        return 0;
    }
    restoring_->resource_ = std::move(resource);
    restore_nodes_ = 0;
    restore_parents_ = 0;
    restore_ready_ = false;
    return restoring_->id();
}

SceneRestoreStatus WorldSystem::step_restore_scene(std::uint64_t request, std::size_t node_budget)
{
    ENGINE_PROFILE_ZONE("World.restoreStep");
    auto* resources = ResourceSystem::instance();
    if (!restoring_ || restoring_->id() != request)
    {
        return SceneRestoreStatus::failed;
    }
    if (!is_initialized() || resources == nullptr ||
        resources->generation() != restoring_->resource_->generation())
    {
        cancel_restore_scene(request);
        return SceneRestoreStatus::failed;
    }
    if (restore_ready_)
    {
        return SceneRestoreStatus::ready;
    }
    const auto& data = *restoring_->resource_->scene();
    const auto started = std::chrono::steady_clock::now();
    const auto count = std::min(node_budget, restore_config_.nodes_per_step);
    for (std::size_t index = 0; index < count; ++index)
    {
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() >=
            restore_config_.max_seconds)
        {
            break;
        }
        bool success = true;
        if (restore_nodes_ < data.nodes.size())
        {
            const auto& node = data.nodes[restore_nodes_++];
            success = node && restoring_->restore_node(*node);
        }
        else if (restore_parents_ < data.nodes.size())
        {
            success = restoring_->restore_parent(*data.nodes[restore_parents_++]);
        }
        else
        {
            success = restoring_->finish_restore(data);
            restore_ready_ = success;
        }
        if (!success)
        {
            cancel_restore_scene(request);
            return SceneRestoreStatus::failed;
        }
        if (restore_ready_)
        {
            return SceneRestoreStatus::ready;
        }
    }
    return SceneRestoreStatus::loading;
}

Scene* WorldSystem::commit_restore_scene(std::uint64_t request)
{
    if (step_restore_scene(request, 0) != SceneRestoreStatus::ready)
    {
        return nullptr;
    }
    scene_ = std::move(restoring_);
    paused_ = false;
    restore_ready_ = false;
    return scene_.get();
}

void WorldSystem::cancel_restore_scene(std::uint64_t request) noexcept
{
    if (restoring_ && restoring_->id() == request)
    {
        restoring_.reset();
        restore_ready_ = false;
    }
}

void WorldSystem::destroy_scene() noexcept
{
    restoring_.reset();
    scene_.reset();
    paused_ = false;
}

Scene* WorldSystem::scene() noexcept
{
    return scene_.get();
}

const Scene* WorldSystem::scene() const noexcept
{
    return scene_.get();
}

bool WorldSystem::set_paused(bool paused) noexcept
{
    if (!is_initialized() || scene_ == nullptr)
    {
        return false;
    }
    paused_ = paused;
    return true;
}

bool WorldSystem::paused() const noexcept
{
    return is_initialized() && scene_ != nullptr && paused_;
}

ComponentRegistry& WorldSystem::component_registry() noexcept
{
    return components_;
}

bool WorldSystem::on_initialize(const EngineConfig&)
{
    if (restore_config_.nodes_per_step == 0 || !std::isfinite(restore_config_.max_seconds) ||
        restore_config_.max_seconds <= 0.0)
    {
        return false;
    }
    paused_ = false;
    components_ = {};
    if (!register_scene_components(components_))
    {
        return false;
    }
    world_ = ecs_init();
    return world_ != nullptr;
}

bool WorldSystem::on_tick(double delta_time)
{
    if (delta_time > static_cast<double>(std::numeric_limits<ecs_ftime_t>::max()))
    {
        return false;
    }
    return world_ != nullptr &&
           (paused() || ecs_progress(world_, static_cast<ecs_ftime_t>(delta_time)));
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
