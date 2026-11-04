#include "runtime/world/world_system.hpp"

#include "runtime/log/log_system.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/bone_attachment.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"

#include <flecs.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <new>
#include <numbers>
#include <string>
#include <utility>

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
    if (!is_initialized() || !can_modify() || scene_ != nullptr ||
        scene_sequence_ == std::numeric_limits<std::uint64_t>::max())
    {
        return nullptr;
    }
    std::unique_ptr<Scene> candidate(new (std::nothrow) Scene(*this, scene_sequence_ + 1));
    if (candidate && candidate->initialize(name) && candidate->publish())
    {
        scene_ = candidate.get();
        scenes_.push_back(std::move(candidate));
        ++scene_sequence_;
        restoring_.reset();
        paused_ = false;
    }
    return scene_;
}

Scene* WorldSystem::load_scene(std::string_view path)
{
    if (!is_initialized() || !can_modify() ||
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
        LogSystem::write(LogLevel::error, "Cannot load scene " + std::string(path) + ": " +
                                              resources->loader().last_error());
        return nullptr;
    }
    const auto* data = resource->scene();
    if (data == nullptr || !data->info)
    {
        return nullptr;
    }
    std::unique_ptr<Scene> candidate(new (std::nothrow) Scene(*this, scene_sequence_ + 1));
    if (!candidate || !candidate->initialize(data->info->name) || !candidate->restore_data(*data) ||
        !candidate->publish())
    {
        LogSystem::write(LogLevel::error, "Cannot restore scene: " + std::string(path));
        return nullptr;
    }
    candidate->resource_ = std::move(resource);
    restoring_.reset();
    retain_persistent();
    scene_ = candidate.get();
    scenes_.push_back(std::move(candidate));
    ++scene_sequence_;
    paused_ = false;
    return scene_;
}

std::uint64_t WorldSystem::begin_restore_scene(std::shared_ptr<const Resource> resource)
{
    if (!is_initialized() || !can_modify() || restoring_ || !resource || !resource->scene() ||
        !resource->scene()->info || resource->scene()->schema_version != 1 ||
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
    restoring_.reset(new (std::nothrow) Scene(*this, ++scene_sequence_));
    if (!restoring_ || !restoring_->initialize(data.info->name))
    {
        restoring_.reset();
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
    if (!can_modify() || !restoring_ || restoring_->id() != request)
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
    if (step_restore_scene(request, 0) != SceneRestoreStatus::ready || !restoring_->publish())
    {
        return nullptr;
    }
    retain_persistent();
    scene_ = restoring_.get();
    scenes_.push_back(std::move(restoring_));
    paused_ = false;
    restore_ready_ = false;
    return scene_;
}

void WorldSystem::cancel_restore_scene(std::uint64_t request) noexcept
{
    if (can_modify() && restoring_ && restoring_->id() == request)
    {
        restoring_.reset();
        restore_ready_ = false;
    }
}

void WorldSystem::destroy_scene() noexcept
{
    if (!can_modify())
    {
        return;
    }
    restoring_.reset();
    restore_ready_ = false;
    scene_ = nullptr;
    scenes_.clear();
    paused_ = false;
}

void WorldSystem::retain_persistent()
{
    scene_ = nullptr;
    for (const auto& scene : scenes_)
    {
        scene->retain_persistent();
    }
    std::erase_if(scenes_, [](const auto& scene) { return scene->empty(); });
}

void WorldSystem::unload_scene() noexcept
{
    if (!can_modify())
    {
        return;
    }
    restoring_.reset();
    restore_ready_ = false;
    retain_persistent();
    paused_ = false;
}

bool WorldSystem::destroy_scene(std::uint64_t id) noexcept
{
    if (!can_modify())
    {
        return false;
    }
    if (scene_ != nullptr && scene_->id() == id)
    {
        scene_ = nullptr;
        paused_ = false;
    }
    return std::erase_if(scenes_, [id](const auto& scene) { return scene->id() == id; }) != 0;
}

Scene* WorldSystem::scene() noexcept
{
    return scene_;
}

const Scene* WorldSystem::scene() const noexcept
{
    return scene_;
}

const std::vector<std::unique_ptr<Scene>>& WorldSystem::scenes() const noexcept
{
    return scenes_;
}

Node WorldSystem::find_node(std::uint64_t id) const noexcept
{
    if (!world_ || id == 0 || !ecs_is_alive(world_, id))
    {
        return {};
    }
    flecs::world current(world_);
    const auto entity = current.entity(id);
    if (!entity.has<ecs_detail::Published>() || !entity.has<ecs_detail::NodeRuntime>())
    {
        return {};
    }
    return Node(*const_cast<WorldSystem*>(this), world_epoch_, id);
}

ecs_world_t* WorldSystem::native_world() const noexcept
{
    return world_;
}
std::uint64_t WorldSystem::epoch() const noexcept
{
    return world_epoch_;
}
bool WorldSystem::can_modify() const noexcept
{
    return world_ && !ecs_is_stage(world_) && !ecs_stage_is_readonly(world_);
}
void WorldSystem::invalidate_scene_state() noexcept
{
    ++state_revision_;
    if (state_revision_ == 0)
    {
        state_revision_ = 1;
        prepared_revision_ = 0;
    }
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
    static std::atomic<std::uint64_t> epochs{0};
    auto previous = epochs.load(std::memory_order_relaxed);
    do
    {
        if (previous == std::numeric_limits<std::uint64_t>::max())
        {
            return false;
        }
    } while (!epochs.compare_exchange_weak(previous, previous + 1, std::memory_order_relaxed));
    world_epoch_ = previous + 1;
    world_ = ecs_init();
    if (!world_)
    {
        return false;
    }
    components_ = {};
    components_.world_ = world_;
    paused_ = false;
    state_revision_ = 1;
    prepared_revision_ = 0;
    flecs::world current(world_);
    current.component<schema::NodePropertiesT>();
    current.component<schema::SceneInfoT>();
    current.component<schema::PrefabInstanceDataT>();
    current.component<ecs_detail::NodeRuntime>();
    current.component<ecs_detail::WorldTransform>();
    current.component<ecs_detail::TransformDrive>();
    current.component<ecs_detail::Published>();
    current.component<ecs_detail::SceneMember>()
        .add(flecs::Exclusive)
        .add(flecs::OnDeleteTarget, flecs::Delete);
    if (!register_scene_components(components_))
    {
        return false;
    }
    current.observer<const schema::NodePropertiesT, const ecs_detail::NodeRuntime>()
        .event(flecs::OnRemove)
        .each(
            [](flecs::entity entity, const schema::NodePropertiesT& properties,
               const ecs_detail::NodeRuntime& state)
            {
                if (state.scene)
                {
                    state.scene->unregister_entity(entity.id(), properties.id);
                }
            });
    ecs_query_desc_t query{};
    query.terms[0].id = current.id<schema::NodePropertiesT>();
    query.terms[1].id = current.id<ecs_detail::NodeRuntime>();
    query.terms[2].id = current.id<ecs_detail::WorldTransform>();
    state_query_ = ecs_query_init(world_, &query);
    return state_query_ != nullptr;
}

bool WorldSystem::on_tick(double delta_time)
{
    if (!can_modify() || !std::isfinite(delta_time) || delta_time < 0.0)
    {
        return false;
    }
    std::erase_if(scenes_,
                  [this](const auto& scene) { return scene.get() != scene_ && scene->empty(); });
    return refresh_scene_state();
}

bool WorldSystem::refresh_scene_state()
{
    if (!world_ || !state_query_)
    {
        return false;
    }
    if (prepared_revision_ == state_revision_)
    {
        return true;
    }
    if (!can_modify())
    {
        return false;
    }
    flecs::world current(world_);
    std::vector<flecs::entity_t> branch;
    auto iterator = ecs_query_iter(world_, state_query_);
    while (ecs_query_next(&iterator))
    {
        for (int32_t row = 0; row < iterator.count; ++row)
        {
            branch.clear();
            auto entity = current.entity(iterator.entities[row]);
            while (entity)
            {
                const auto* state = entity.try_get<ecs_detail::NodeRuntime>();
                if (!state || state->prepared_revision == state_revision_)
                {
                    break;
                }
                branch.push_back(entity.id());
                entity = entity.parent();
            }
            for (auto item = branch.rbegin(); item != branch.rend(); ++item)
            {
                auto node = current.entity(*item);
                auto* state = node.try_get_mut<ecs_detail::NodeRuntime>();
                auto* matrix = node.try_get_mut<ecs_detail::WorldTransform>();
                const auto* properties = node.try_get<schema::NodePropertiesT>();
                if (!state || !matrix || !properties)
                {
                    continue;
                }
                const auto parent = node.parent();
                const auto* parent_state =
                    parent ? parent.try_get<ecs_detail::NodeRuntime>() : nullptr;
                const auto* parent_matrix =
                    parent ? parent.try_get<ecs_detail::WorldTransform>() : nullptr;
                state->active =
                    properties->enabled && (!parent || (parent_state && parent_state->active));
                state->visible_in_hierarchy =
                    properties->visible &&
                    (!parent || (parent_state && parent_state->visible_in_hierarchy));
                const auto* transform =
                    node.try_get<ecs_detail::ComponentStorage<schema::TransformDataT>>();
                *matrix = {};
                matrix->valid = transform && (!parent || (parent_matrix && parent_matrix->valid));
                if (matrix->valid)
                {
                    const auto& value = transform->data;
                    constexpr double radians = std::numbers::pi / 180.0;
                    const double sx = std::sin(value.rotation.x() * radians);
                    const double cx = std::cos(value.rotation.x() * radians);
                    const double sy = std::sin(value.rotation.y() * radians);
                    const double cy = std::cos(value.rotation.y() * radians);
                    const double sz = std::sin(value.rotation.z() * radians);
                    const double cz = std::cos(value.rotation.z() * radians);
                    std::array<double, 16> local{cz * cy * value.scale.x(),
                                                 sz * cy * value.scale.x(),
                                                 -sy * value.scale.x(),
                                                 0,
                                                 (cz * sy * sx - sz * cx) * value.scale.y(),
                                                 (sz * sy * sx + cz * cx) * value.scale.y(),
                                                 cy * sx * value.scale.y(),
                                                 0,
                                                 (cz * sy * cx + sz * sx) * value.scale.z(),
                                                 (sz * sy * cx - cz * sx) * value.scale.z(),
                                                 cy * cx * value.scale.z(),
                                                 0,
                                                 value.position.x(),
                                                 value.position.y(),
                                                 value.position.z(),
                                                 1};
                    const auto* drive = node.try_get<ecs_detail::TransformDrive>();
                    if (drive && drive->active &&
                        drive->owner_type == BoneAttachment::component_type)
                    {
                        const auto* attachment =
                            node.try_get<ecs_detail::ComponentStorage<BoneAttachment::Data>>();
                        const auto* follow = node.try_get<BoneAttachment::Runtime>();
                        if (attachment && follow &&
                            attachment->state.instance_id == drive->owner_instance_id &&
                            attachment->state.flags.enabled && attachment->data.follow_enabled &&
                            state->active && transform->state.flags.enabled && follow->source &&
                            follow->source.enabled() && follow->source.node().enabled() &&
                            follow->source.runtime()->pose_revision_ != 0)
                        {
                            bool source_active = true;
                            for (auto ancestor = follow->source.node(); ancestor;
                                 ancestor = ancestor.parent())
                            {
                                if (!ancestor.enabled() ||
                                    !ancestor.get_component<Transform>().enabled())
                                {
                                    source_active = false;
                                    break;
                                }
                            }
                            if (source_active)
                            {
                                std::copy(drive->local.begin(), drive->local.end(), local.begin());
                            }
                        }
                    }
                    std::array<double, 16> result = local;
                    if (parent)
                    {
                        result = {};
                        for (std::size_t column = 0; column < 4; ++column)
                        {
                            for (std::size_t line = 0; line < 4; ++line)
                            {
                                for (std::size_t index = 0; index < 4; ++index)
                                {
                                    result[column * 4 + line] +=
                                        parent_matrix->matrix[index * 4 + line] *
                                        local[column * 4 + index];
                                }
                            }
                        }
                    }
                    for (std::size_t index = 0; index < 16; ++index)
                    {
                        if (!std::isfinite(result[index]) ||
                            std::abs(result[index]) > std::numeric_limits<float>::max())
                        {
                            matrix->valid = false;
                            break;
                        }
                        matrix->matrix[index] = static_cast<float>(result[index]);
                    }
                    if (!matrix->valid)
                    {
                        matrix->matrix = ecs_detail::WorldTransform{}.matrix;
                    }
                }
                state->prepared_revision = state_revision_;
            }
        }
    }
    prepared_revision_ = state_revision_;
    return true;
}

void WorldSystem::on_shutdown() noexcept
{
    destroy_scene();
    if (state_query_)
    {
        ecs_query_fini(state_query_);
        state_query_ = nullptr;
    }
    if (world_)
    {
        if (ecs_fini(world_) != 0)
        {
            std::fputs("Cannot finish the ECS world cleanly\n", stderr);
        }
        world_ = nullptr;
    }
    components_ = {};
}
} // namespace engine
