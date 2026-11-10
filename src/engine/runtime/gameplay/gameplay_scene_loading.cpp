#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/gameplay/scene_load_operation.hpp"

#include "runtime/animation/animation_system.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_loader.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/ui/ui_system.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>

namespace engine
{
namespace
{
std::uint64_t load_time()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}
bool terminal(SceneLoadState state)
{
    return state == SceneLoadState::active || state == SceneLoadState::failed ||
           state == SceneLoadState::cancelled;
}
} // namespace

bool GameplaySession::begin_scene_load(const SceneLoadSpec& spec, SceneLoadId& output)
{
    output = {};
    auto* loader = world_.resource_loader();
    if (!loader || !world_.is_initialized() || scene_loads_.size() >= 64 ||
        scene_load_sequence_ == UINT64_MAX || spec.preload_roots.size() > 64 ||
        (spec.kind == SceneLoadKind::replace_scene && spec.scene_path.empty()) ||
        (spec.kind == SceneLoadKind::append_batch && !spec.scene_path.empty()))
    {
        last_error_ = "Cannot accept scene loading request";
        return false;
    }
    for (const auto& [key, operation] : scene_loads_)
    {
        (void)key;
        if (spec.kind == SceneLoadKind::replace_scene &&
            operation->spec.kind == SceneLoadKind::replace_scene &&
            !terminal(operation->snapshot.state))
        {
            last_error_ = "Another scene replacement is still pending";
            return false;
        }
    }
    if (scene_load_owner_ == 0)
    {
        static std::atomic<std::uint64_t> owners{0};
        auto previous = owners.load();
        do
        {
            if (previous == UINT64_MAX)
            {
                last_error_ = "Scene loading identity space is exhausted";
                return false;
            }
        } while (!owners.compare_exchange_weak(previous, previous + 1));
        scene_load_owner_ = previous + 1;
    }
    auto operation = std::make_unique<SceneLoadOperation>();
    operation->spec = spec;
    operation->started_ns = load_time();
    if (!world_.begin_scope(spec.kind, spec.destination_scene_id, operation->snapshot.scope))
    {
        last_error_ = "Cannot create scene candidate scope";
        return false;
    }
    auto roots = spec.preload_roots;
    if (!spec.scene_path.empty())
    {
        roots.insert(roots.begin(), spec.scene_path);
    }
    for (const auto& path : roots)
    {
        std::unique_ptr<ResourceLoad> request;
        if (!loader->begin_load(path, request))
        {
            last_error_ = loader->last_error();
            (void)world_.cancel_scope(operation->snapshot.scope);
            world_.release_scope(operation->snapshot.scope);
            return false;
        }
        operation->resources.push_back(std::move(request));
    }
    operation->retained.resize(roots.size());
    operation->snapshot.id = {scene_load_owner_, ++scene_load_sequence_};
    operation->snapshot.scene_id = spec.kind == SceneLoadKind::replace_scene
                                       ? operation->snapshot.scope.value
                                       : spec.destination_scene_id;
    operation->snapshot.target_alive = true;
    operation->snapshot.last_progress_ns = load_time();
    output = operation->snapshot.id;
    scene_loads_.emplace(output.request, std::move(operation));
    return true;
}

Scene* GameplaySession::scene_load_scene(SceneLoadId id) noexcept
{
    const auto found = scene_loads_.find(id.request);
    if (id.owner != scene_load_owner_ || found == scene_loads_.end())
    {
        return nullptr;
    }
    const auto& value = *found->second;
    if (value.snapshot.state == SceneLoadState::staged)
    {
        return world_.scope_candidate(value.snapshot.scope);
    }
    return value.snapshot.state == SceneLoadState::active && world_.scene() &&
                   world_.scene()->id() == value.snapshot.scene_id
               ? world_.scene()
               : nullptr;
}

bool GameplaySession::prepare_scene_load(SceneLoadId id)
{
    const auto found = scene_loads_.find(id.request);
    if (id.owner != scene_load_owner_ || found == scene_loads_.end())
    {
        return false;
    }
    auto& snapshot = found->second->snapshot;
    if (snapshot.state == SceneLoadState::preparing || snapshot.state == SceneLoadState::ready)
    {
        return true;
    }
    if (snapshot.state != SceneLoadState::staged || !world_.seal_scope(snapshot.scope))
    {
        return false;
    }
    snapshot.state = SceneLoadState::preparing;
    ++snapshot.progress_revision;
    snapshot.last_progress_ns = load_time();
    return true;
}

bool GameplaySession::scene_load_status(SceneLoadId id, SceneLoadSnapshot& output) const
{
    output = {};
    const auto found = scene_loads_.find(id.request);
    if (id.owner != scene_load_owner_ || found == scene_loads_.end())
    {
        return false;
    }
    output = found->second->snapshot;
    output.target_alive =
        const_cast<WorldSystem&>(world_).scope_candidate(output.scope) != nullptr &&
        (output.state != SceneLoadState::active ||
         (world_.scene() && world_.scene()->id() == output.scene_id));
    return true;
}

bool GameplaySession::activate_scene_load(SceneLoadId id)
{
    const auto found = scene_loads_.find(id.request);
    if (id.owner != scene_load_owner_ || found == scene_loads_.end())
    {
        return false;
    }
    auto& operation = *found->second;
    if (operation.snapshot.state == SceneLoadState::activating ||
        operation.snapshot.state == SceneLoadState::active)
    {
        return true;
    }
    if (operation.snapshot.state != SceneLoadState::ready)
    {
        return false;
    }
    operation.activate = true;
    operation.snapshot.state = SceneLoadState::activating;
    return true;
}

bool GameplaySession::cancel_scene_load(SceneLoadId id)
{
    const auto found = scene_loads_.find(id.request);
    if (id.owner != scene_load_owner_ || found == scene_loads_.end())
    {
        return false;
    }
    auto& operation = *found->second;
    if (operation.snapshot.state == SceneLoadState::cancelled)
    {
        return true;
    }
    if (terminal(operation.snapshot.state) ||
        operation.snapshot.state == SceneLoadState::activating)
    {
        return false;
    }
    if (physics_)
    {
        physics_->release_scope(operation.snapshot.scope);
    }
    if (animation_)
    {
        animation_->release_scope(operation.snapshot.scope);
    }
    if (ui_)
    {
        ui_->release_scope(operation.snapshot.scope);
    }
    if (!world_.cancel_scope(operation.snapshot.scope))
    {
        return false;
    }
    operation.resources.clear();
    operation.retained.clear();
    operation.snapshot.state = SceneLoadState::cancelled;
    renderer_.release_scope(operation.snapshot.scope);
    return true;
}

bool GameplaySession::release_scene_load(SceneLoadId id)
{
    const auto found = scene_loads_.find(id.request);
    if (id.owner != scene_load_owner_ || found == scene_loads_.end() ||
        !terminal(found->second->snapshot.state))
    {
        return false;
    }
    world_.release_scope(found->second->snapshot.scope);
    scene_loads_.erase(found);
    return true;
}

bool GameplaySession::collect_scene_loads(std::vector<SceneLoadId>& output) const
{
    output.clear();
    for (const auto& [key, operation] : scene_loads_)
    {
        (void)key;
        output.push_back(operation->snapshot.id);
    }
    return true;
}

void GameplaySession::step_scene_loads(const SceneWorkBudget& budget)
{
    const auto started = load_time();
    for (auto& [key, operation] : scene_loads_)
    {
        (void)key;
        auto& op = *operation;
        auto& snapshot = op.snapshot;
        if (terminal(snapshot.state) || snapshot.state == SceneLoadState::staged ||
            snapshot.state == SceneLoadState::ready)
        {
            continue;
        }
        const double remaining = budget.seconds - (load_time() - started) * 1e-9;
        if (remaining <= 0)
        {
            break;
        }
        auto step = budget;
        step.seconds = remaining;
        const auto previous_state = snapshot.state;
        const auto old_stages = snapshot.stages;
        const auto fail = [&](std::string code, std::string error)
        {
            snapshot.state = SceneLoadState::failed;
            snapshot.error_code = std::move(code);
            snapshot.error = std::move(error);
            if (physics_)
            {
                physics_->release_scope(snapshot.scope);
            }
            if (animation_)
            {
                animation_->release_scope(snapshot.scope);
            }
            if (ui_)
            {
                ui_->release_scope(snapshot.scope);
            }
            (void)world_.cancel_scope(snapshot.scope);
            renderer_.release_scope(snapshot.scope);
            ++snapshot.progress_revision;
            snapshot.last_progress_ns = load_time();
        };
        if (snapshot.state == SceneLoadState::loading ||
            snapshot.state == SceneLoadState::preparing)
        {
            snapshot.stages.clear();
        }
        if (snapshot.state == SceneLoadState::loading)
        {
            SceneWorkProgress progress;
            progress.domain = "resource";
            progress.stage = "loading";
            progress.state = SceneWorkState::ready;
            progress.total_known = true;
            bool ready = true;
            for (std::size_t index = 0; index < op.resources.size(); ++index)
            {
                ResourceLoadProgress value;
                const auto state = op.resources[index]->status(value);
                progress.completed += value.completed;
                progress.total += value.total;
                progress.total_known = progress.total_known && value.discovery_complete;
                progress.in_flight += value.in_flight;
                progress.progress_revision += value.progress_revision;
                progress.last_progress_ns =
                    std::max(progress.last_progress_ns, value.last_progress_ns);
                if (state == ResourceLoadStatus::failed || state == ResourceLoadStatus::cancelled)
                {
                    fail("resource_failed", std::string(op.resources[index]->last_error()));
                    break;
                }
                if (state == ResourceLoadStatus::ready)
                {
                    ResourceHandle handle;
                    if (!op.resources[index]->take(handle) ||
                        !op.retained[index].adopt(world_.resource_loader(), handle))
                    {
                        fail("resource_failed", "Cannot retain loaded scene dependency");
                        break;
                    }
                }
                ready = ready && static_cast<bool>(op.retained[index]);
            }
            if (snapshot.state == SceneLoadState::failed)
            {
                continue;
            }
            if (ready && op.resource_ready_ns == 0)
            {
                op.resource_ready_ns = load_time();
            }
            progress.elapsed_ns =
                (op.resource_ready_ns ? op.resource_ready_ns : load_time()) - op.started_ns;
            progress.state = ready ? SceneWorkState::ready : SceneWorkState::working;
            snapshot.stages.push_back(progress);
            if (ready)
            {
                if (!op.restored)
                {
                    if (!op.spec.scene_path.empty() &&
                        !world_.restore_scope(snapshot.scope, op.retained.front().handle()))
                    {
                        fail("restore_failed", "Cannot begin scene restoration");
                        continue;
                    }
                    op.restored = true;
                }
                const auto state = world_.step_scope(snapshot.scope, step);
                ScenePreparationSnapshot world_status;
                if (world_.scope_status(snapshot.scope, world_status))
                {
                    snapshot.stages.insert(snapshot.stages.end(), world_status.stages.begin(),
                                           world_status.stages.end());
                }
                if (state == SceneWorkState::failed)
                {
                    fail("restore_failed", "Cannot restore scene candidate");
                }
                else if (state == SceneWorkState::ready)
                {
                    snapshot.state = SceneLoadState::staged;
                }
            }
        }
        else if (snapshot.state == SceneLoadState::preparing)
        {
            bool ready = true;
            const auto collect = [&](SceneWorkState state, const ScenePreparationSnapshot& value)
            {
                snapshot.stages.insert(snapshot.stages.end(), value.stages.begin(),
                                       value.stages.end());
                ready = ready &&
                        (state == SceneWorkState::ready || state == SceneWorkState::not_required);
                if (state == SceneWorkState::failed)
                {
                    fail("preparation_failed",
                         value.stages.empty() ? "Cannot prepare scene" : value.stages.back().error);
                }
            };
            ScenePreparationSnapshot value;
            auto state = world_.step_scope(snapshot.scope, step);
            (void)world_.scope_status(snapshot.scope, value);
            collect(state, value);
            const auto prepare_domain = [&](auto* system)
            {
                if (!ready || !system || snapshot.state == SceneLoadState::failed)
                {
                    return;
                }
                step.seconds = budget.seconds - (load_time() - started) * 1e-9;
                if (step.seconds <= 0)
                {
                    ready = false;
                    return;
                }
                const auto domain_state = system->prepare_scope(snapshot.scope, step);
                ScenePreparationSnapshot domain_status;
                (void)system->scope_status(snapshot.scope, domain_status);
                collect(domain_state, domain_status);
            };
            prepare_domain(physics_);
            prepare_domain(animation_);
            prepare_domain(ui_);
            if (ready && snapshot.state != SceneLoadState::failed)
            {
                step.seconds = budget.seconds - (load_time() - started) * 1e-9;
                if (step.seconds <= 0)
                {
                    ready = false;
                    continue;
                }
                state = renderer_.prepare_scope(snapshot.scope, step, op.spec.view_camera);
                (void)renderer_.scope_status(snapshot.scope, value);
                collect(state, value);
            }
            if (ready && snapshot.state != SceneLoadState::failed)
            {
                snapshot.state = SceneLoadState::ready;
            }
        }
        else if (snapshot.state == SceneLoadState::activating)
        {
            if (op.activate)
            {
                if (!world_.publish_scope(snapshot.scope))
                {
                    fail("publish_failed", "Cannot publish prepared scene scope");
                    continue;
                }
                op.activate = false;
            }
            RenderFrameSnapshot frame;
            if (renderer_.frame_status(snapshot.scope, frame) &&
                world_.activate_scope(snapshot.scope))
            {
                snapshot.first_scene_submission_id = frame.submission_id;
                snapshot.state = SceneLoadState::active;
            }
        }
        bool progressed =
            previous_state != snapshot.state || old_stages.size() != snapshot.stages.size();
        for (std::size_t index = 0; !progressed && index < old_stages.size(); ++index)
        {
            progressed =
                old_stages[index].completed != snapshot.stages[index].completed ||
                old_stages[index].state != snapshot.stages[index].state ||
                old_stages[index].stage != snapshot.stages[index].stage ||
                old_stages[index].total != snapshot.stages[index].total ||
                old_stages[index].total_known != snapshot.stages[index].total_known ||
                old_stages[index].in_flight != snapshot.stages[index].in_flight ||
                old_stages[index].progress_revision != snapshot.stages[index].progress_revision;
        }
        if (progressed)
        {
            ++snapshot.progress_revision;
            snapshot.last_progress_ns = load_time();
        }
    }
}
} // namespace engine
