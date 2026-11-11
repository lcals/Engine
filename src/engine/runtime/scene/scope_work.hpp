#pragma once
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/scene_loading.hpp"
#include "runtime/world/world_system.hpp"
#include <chrono>
#include <cmath>
#include <flecs.h>

namespace engine::scene_detail
{
inline std::uint64_t work_time() noexcept
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}
struct ScopeWrite
{
    std::uint64_t epoch = ecs_detail::preparing_epoch;
    std::uint64_t scope = ecs_detail::preparing_scope;
    explicit ScopeWrite(SceneScopeId id)
    {
        ecs_detail::preparing_epoch = id.world_epoch;
        ecs_detail::preparing_scope = id.value;
    }
    ~ScopeWrite()
    {
        ecs_detail::preparing_epoch = epoch;
        ecs_detail::preparing_scope = scope;
    }
};
struct ScopeWork
{
    ScenePreparationSnapshot snapshot;
    std::vector<Node> nodes;
    std::size_t next = 0;
    std::uint64_t started = 0;
    bool finalized = false;
};
inline bool begin_work(WorldSystem& world, SceneScopeId scope, std::string_view domain,
                       ScopeWork& work)
{
    auto* candidate = world.scope_candidate(scope);
    if (!candidate)
    {
        return false;
    }
    work.snapshot.scope = scope;
    work.snapshot.state = SceneWorkState::working;
    work.started = work_time();
    auto query =
        flecs::world(world.native_world()).query_builder<const ecs_detail::NodeRuntime>().build();
    query.each(
        [&](flecs::entity entity, const ecs_detail::NodeRuntime& runtime)
        {
            if (runtime.scope_scene == candidate)
            {
                work.nodes.push_back(candidate->find_node(entity.id()));
            }
            else if (candidate == world.scene() && entity.has<ecs_detail::Published>())
            {
                // 常驻节点仍归原场景拥有，由世界查询其已发布身份。
                work.nodes.push_back(world.find_node(entity.id()));
            }
        });
    SceneWorkProgress progress;
    progress.domain = domain;
    progress.stage = "preparing";
    progress.state = SceneWorkState::working;
    progress.total_known = true;
    progress.total = work.nodes.size();
    work.snapshot.stages = {std::move(progress)};
    return true;
}
template <class Prepare>
SceneWorkState step_work(ScopeWork& work, const SceneWorkBudget& budget, Prepare prepare)
{
    if (work.snapshot.state == SceneWorkState::ready ||
        work.snapshot.state == SceneWorkState::failed)
    {
        return work.snapshot.state;
    }
    if (budget.items == 0 || !std::isfinite(budget.seconds) || budget.seconds <= 0)
    {
        return SceneWorkState::failed;
    }
    const ScopeWrite write(work.snapshot.scope);
    auto& progress = work.snapshot.stages.front();
    const auto started = work_time();
    const auto before = work.next;
    for (std::uint32_t item = 0; item < budget.items && work.next < work.nodes.size(); ++item)
    {
        if (item != 0 && (work_time() - started) * 1e-9 >= budget.seconds)
        {
            break;
        }
        if (!work.nodes[work.next] || !prepare(work.nodes[work.next]))
        {
            work.snapshot.state = progress.state = SceneWorkState::failed;
            progress.error_code = "preparation_failed";
            progress.error = "Cannot prepare required scene component";
            return work.snapshot.state;
        }
        ++work.next;
    }
    progress.completed = work.next;
    progress.elapsed_ns = work_time() - work.started;
    if (work.next != before)
    {
        ++progress.progress_revision;
        progress.last_progress_ns = work_time();
    }
    work.snapshot.state = progress.state =
        work.next == work.nodes.size() ? SceneWorkState::ready : SceneWorkState::working;
    return work.snapshot.state;
}
} // namespace engine::scene_detail
