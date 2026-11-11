#include "runtime/world/world_scene_scope.hpp"

#include "runtime/resource/types/scene_resource.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/resource_binding.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace engine
{
namespace
{
std::uint64_t scope_time()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}
} // namespace

bool WorldSystem::begin_scope(SceneLoadKind kind, std::uint64_t destination, SceneScopeId& output)
{
    output = {};
    if (!is_initialized() || !can_modify() || scopes_.size() >= 256 ||
        scene_sequence_ == std::numeric_limits<std::uint64_t>::max() ||
        (kind == SceneLoadKind::append_batch && (!scene_ || scene_->id() != destination)) ||
        (kind == SceneLoadKind::replace_scene && destination != 0))
    {
        return false;
    }
    auto scope = std::make_unique<Scope>();
    scope->id = {world_epoch_, ++scene_sequence_};
    scope->kind = kind;
    scope->started_ns = scope_time();
    scope->destination = destination;
    scope->candidate.reset(new Scene(*this, scope->id.value));
    if (!scope->candidate->initialize("Scene candidate"))
    {
        return false;
    }
    scope->candidate->allocation_target_ = kind == SceneLoadKind::append_batch ? scene_ : nullptr;
    scope->progress.domain = "world";
    scope->progress.stage = "staged";
    scope->progress.state = SceneWorkState::ready;
    scope->progress.total_known = true;
    flecs::world current(world_);
    current.entity(scope->candidate->metadata_entity_)
        .set<ecs_detail::ScopeState>({scope->id.value, false, false, false, 0, 0, destination});
    output = scope->id;
    scopes_.emplace(output.value, std::move(scope));
    return true;
}

bool WorldSystem::restore_scope(SceneScopeId id, const ResourceHandle& handle)
{
    const auto found = scopes_.find(id.value);
    if (id.world_epoch != world_epoch_ || found == scopes_.end() || !can_modify())
    {
        return false;
    }
    auto& scope = *found->second;
    if (scope.sealed || scope.published || !scope.candidate || scope.resource ||
        scope.kind != SceneLoadKind::replace_scene || !scope.candidate->empty() ||
        !scope.resource.retain(resources_, handle))
    {
        return false;
    }
    const auto* data = scope.resource.data<SceneResource>();
    if (!data || !data->info || data->schema_version != 3)
    {
        scope.resource.reset();
        return false;
    }
    scope.restored = false;
    scope.started_ns = scope_time();
    scope.progress = {};
    scope.progress.domain = "world";
    scope.progress.stage = "restoring";
    scope.progress.state = SceneWorkState::working;
    scope.progress.total = data->nodes.size() * 2 + 1;
    scope.progress.total_known = true;
    return true;
}

SceneWorkState WorldSystem::step_scope(SceneScopeId id, const SceneWorkBudget& budget)
{
    const auto found = scopes_.find(id.value);
    if (id.world_epoch != world_epoch_ || found == scopes_.end() || !can_modify() ||
        !std::isfinite(budget.seconds) || budget.seconds <= 0)
    {
        return SceneWorkState::failed;
    }
    auto& scope = *found->second;
    if (!scope.candidate || scope.progress.state == SceneWorkState::failed ||
        scope.progress.state == SceneWorkState::cancelled)
    {
        return scope.progress.state;
    }
    const auto before = scope.progress.completed;
    const auto previous_state = scope.progress.state;
    const auto started = scope_time();
    const auto fail = [&]()
    {
        scope.progress.state = SceneWorkState::failed;
        scope.progress.error_code = "restore_failed";
        scope.progress.error = "Cannot restore or map the candidate scene";
        return SceneWorkState::failed;
    };
    const auto count = std::min<std::size_t>(budget.items, restore_config_.nodes_per_step);
    ecs_detail::ResourceBindingScope binding(*this, scope.resource.handle());
    for (std::size_t item = 0; item < count; ++item)
    {
        if (item != 0 && (scope_time() - started) * 1e-9 >= budget.seconds)
        {
            break;
        }
        if (!scope.restored)
        {
            const auto* data = scope.resource.data<SceneResource>();
            if (!data)
            {
                return fail();
            }
            bool success = false;
            if (scope.nodes < data->nodes.size())
            {
                const auto& node = data->nodes[scope.nodes++];
                success = node && scope.candidate->restore_node(*node);
            }
            else if (scope.parents < data->nodes.size())
            {
                success = scope.candidate->restore_parent(*data->nodes[scope.parents++]);
            }
            else
            {
                success = scope.candidate->finish_restore(*data);
                scope.restored = success;
                if (success)
                {
                    scope.candidate->resource_ = scope.resource.detach();
                }
            }
            if (!success)
            {
                return fail();
            }
            ++scope.progress.completed;
        }
        else if (scope.sealed && scope.mapped < scope.members.size())
        {
            const auto node = scope.members[scope.mapped++];
            if (!node)
            {
                return fail();
            }
            if (scope.kind == SceneLoadKind::append_batch)
            {
                if (!scene_ || scene_->id() != scope.destination)
                {
                    return fail();
                }
                flecs::world current(world_);
                auto* state = current.entity(node.id()).try_get_mut<ecs_detail::NodeRuntime>();
                if (!state || !scene_->local_node_index_.emplace(node.local_id(), node.id()).second)
                {
                    return fail();
                }
                state->scene = scene_;
            }
            ++scope.progress.completed;
        }
        else if (scope.sealed && (scope.previous_prepared < scope.previous_nodes.size() ||
                                  scope.previous_batch < scope.previous_batches.size()))
        {
            Node node;
            if (scope.previous_prepared < scope.previous_nodes.size())
            {
                node = scope.previous_nodes[scope.previous_prepared++];
            }
            else
            {
                const auto& batch = *scope.previous_batches[scope.previous_batch];
                if (scope.previous_batch_node >= batch.size())
                {
                    ++scope.previous_batch;
                    scope.previous_batch_node = 0;
                    continue;
                }
                node = batch[scope.previous_batch_node++];
            }
            if (node)
            {
                scope.previous_entities.insert(node.id());
                auto root = node;
                while (root.parent())
                {
                    root = root.parent();
                }
                scope.prepared_roots[node.id()] = {root.id(), root.persistent()};
                if (root.persistent())
                {
                    flecs::world current(world_);
                    current.entity(node.id())
                        .add<ecs_detail::Published>()
                        .add<ecs_detail::Simulating>();
                }
                else
                {
                    flecs::world current(world_);
                    current.entity(node.id())
                        .remove<ecs_detail::Published>()
                        .remove<ecs_detail::Simulating>();
                    if (!node.parent())
                    {
                        scope.retirement_roots.push_back(node);
                    }
                }
            }
            ++scope.progress.completed;
        }
        else
        {
            break;
        }
    }
    const bool complete =
        scope.restored &&
        (!scope.sealed || (scope.mapped == scope.members.size() &&
                           scope.previous_prepared == scope.previous_nodes.size() &&
                           scope.previous_batch == scope.previous_batches.size()));
    scope.progress.state = complete ? SceneWorkState::ready : SceneWorkState::working;
    if (before != scope.progress.completed || previous_state != scope.progress.state)
    {
        ++scope.progress.progress_revision;
        scope.progress.last_progress_ns = scope_time();
    }
    scope.progress.elapsed_ns = scope_time() - scope.started_ns;
    return scope.progress.state;
}

Scene* WorldSystem::scope_candidate(SceneScopeId id) const noexcept
{
    const auto found = scopes_.find(id.value);
    if (id.world_epoch != world_epoch_)
    {
        return nullptr;
    }
    if (found == scopes_.end())
    {
        for (const auto& scene : scenes_)
        {
            if (scene->id() == id.value)
            {
                return scene.get();
            }
        }
        return nullptr;
    }
    const auto& scope = *found->second;
    if (scope.candidate)
    {
        return scope.candidate.get();
    }
    for (const auto& scene : scenes_)
    {
        if (scene.get() == scope.published_scene)
        {
            return scope.kind == SceneLoadKind::append_batch
                       ? (scene_ && scene_->id() == scope.destination ? scene_ : nullptr)
                       : scene.get();
        }
    }
    return nullptr;
}

bool WorldSystem::seal_scope(SceneScopeId id)
{
    const auto found = scopes_.find(id.value);
    if (id.world_epoch != world_epoch_ || found == scopes_.end() || !can_modify())
    {
        return false;
    }
    auto& scope = *found->second;
    if (scope.sealed)
    {
        return scope.progress.state != SceneWorkState::failed;
    }
    if (!scope.restored || !scope.candidate)
    {
        return false;
    }
    scope.members.reserve(scope.candidate->local_node_index_.size());
    auto query = flecs::world(world_).query<const ecs_detail::NodeRuntime>();
    query.each(
        [&](flecs::entity entity, const ecs_detail::NodeRuntime& runtime)
        {
            const auto node = Node(*this, world_epoch_, entity.id());
            if (runtime.scope_scene == scope.candidate.get())
            {
                scope.members.push_back(node);
            }
            else if (scope.kind == SceneLoadKind::replace_scene &&
                     entity.has<ecs_detail::Published>())
            {
                scope.previous_nodes.push_back(node);
                scope.previous_entities.insert(node.id());
            }
        });
    scope.sealed = true;
    scope.progress.stage = "mapping";
    scope.started_ns = scope_time();
    scope.progress.completed = 0;
    scope.progress.elapsed_ns = 0;
    ++scope.progress.progress_revision;
    scope.progress.last_progress_ns = scope.started_ns;
    scope.progress.total = scope.members.size() + scope.previous_nodes.size();
    scope.progress.state = SceneWorkState::working;
    flecs::world current(world_);
    current.entity(scope.candidate->metadata_entity_).get_mut<ecs_detail::ScopeState>().sealed =
        true;
    return true;
}

bool WorldSystem::scope_status(SceneScopeId id, ScenePreparationSnapshot& output) const
{
    output = {};
    const auto found = scopes_.find(id.value);
    if (id.world_epoch != world_epoch_ || found == scopes_.end())
    {
        return false;
    }
    output.scope = id;
    output.state = found->second->progress.state;
    output.stages.push_back(found->second->progress);
    return true;
}

bool WorldSystem::publish_scope(SceneScopeId id)
{
    const auto found = scopes_.find(id.value);
    if (id.world_epoch != world_epoch_ || found == scopes_.end() || !can_modify())
    {
        return false;
    }
    auto& scope = *found->second;
    if (scope.published)
    {
        return true;
    }
    if (!scope.sealed || !scope.candidate || scope.progress.state != SceneWorkState::ready ||
        (scope.kind == SceneLoadKind::append_batch &&
         (!scene_ || scene_->id() != scope.destination)))
    {
        return false;
    }
    flecs::world current(world_);
    auto metadata = current.entity(scope.candidate->metadata_entity_);
    metadata.add<ecs_detail::Published>();
    auto& state = metadata.get_mut<ecs_detail::ScopeState>();
    state.published = true;
    if (scope.kind == SceneLoadKind::append_batch)
    {
        auto& destination =
            current.entity(scene_->metadata_entity_).get_mut<ecs_detail::ScopeState>();
        state.publication_revision = ++destination.content_revision;
    }
    else
    {
        state.content_revision = state.publication_revision = 1;
    }
    scope.candidate->published_ = true;
    scope.published_scene = scope.candidate.get();
    if (scope.kind == SceneLoadKind::replace_scene)
    {
        for (const auto& old : scenes_)
        {
            auto metadata_old = current.entity(old->metadata_entity_);
            metadata_old.remove<ecs_detail::Published>().remove<ecs_detail::Simulating>();
            auto& previous_state = metadata_old.get_mut<ecs_detail::ScopeState>();
            previous_state.sealed = true;
            previous_state.simulating = false;
        }
        scene_ = scope.published_scene;
        paused_ = false;
    }
    scenes_.push_back(std::move(scope.candidate));
    scope.published = true;
    invalidate_scene_state();
    return true;
}

bool WorldSystem::activate_scope(SceneScopeId id)
{
    const auto found = scopes_.find(id.value);
    if (id.world_epoch != world_epoch_ || found == scopes_.end() || !can_modify())
    {
        return false;
    }
    auto& scope = *found->second;
    if (!scope.published || !scope.published_scene)
    {
        return false;
    }
    if (scope.active)
    {
        return true;
    }
    flecs::world current(world_);
    auto metadata = current.entity(scope.published_scene->metadata_entity_);
    metadata.add<ecs_detail::Simulating>();
    metadata.get_mut<ecs_detail::ScopeState>().simulating = true;
    scope.active = true;
    if (scope.kind == SceneLoadKind::append_batch && !scope.members.empty())
    {
        std::shared_ptr<const std::vector<Node>> batch;
        for (auto& [key, preparing] : scopes_)
        {
            (void)key;
            if (preparing->kind != SceneLoadKind::replace_scene || !preparing->sealed ||
                preparing->published)
            {
                continue;
            }
            if (!batch)
            {
                batch = std::make_shared<const std::vector<Node>>(std::move(scope.members));
            }
            preparing->previous_batches.push_back(batch);
            preparing->progress.total += batch->size();
            preparing->progress.state = SceneWorkState::working;
            ++preparing->progress.progress_revision;
            preparing->progress.last_progress_ns = scope_time();
        }
    }
    if (scope.kind == SceneLoadKind::replace_scene)
    {
        for (auto root : scope.retirement_roots)
        {
            retirement_.emplace_back(root, false);
        }
        for (const auto& old : scenes_)
        {
            if (old.get() != scope.published_scene && old->resource_.id)
            {
                (void)resources_->release(old->resource_);
                old->resource_ = {};
            }
        }
        scope.previous_nodes.clear();
        scope.previous_entities.clear();
        scope.previous_batches.clear();
        scope.prepared_roots.clear();
        scope.retirement_roots.clear();
    }
    return true;
}

bool WorldSystem::cancel_scope(SceneScopeId id)
{
    const auto found = scopes_.find(id.value);
    if (id.world_epoch != world_epoch_ || found == scopes_.end() || !can_modify() ||
        found->second->published)
    {
        return false;
    }
    auto& scope = *found->second;
    scope.candidate.reset();
    scope.resource.reset();
    scope.members.clear();
    scope.progress.state = SceneWorkState::cancelled;
    return true;
}

void WorldSystem::release_scope(SceneScopeId id) noexcept
{
    const auto found = scopes_.find(id.value);
    if (id.world_epoch == world_epoch_ && found != scopes_.end() && !found->second->published)
    {
        scopes_.erase(found);
    }
}
} // namespace engine
