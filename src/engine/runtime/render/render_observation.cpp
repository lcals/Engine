#include "runtime/render/frame_counters.hpp"
#include "runtime/render/passes/frame_output.hpp"
#include "runtime/render/render_extension.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/render/renderer_callbacks.hpp"
#include "runtime/render/scene_gpu_resources.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/world/world_system.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <flecs.h>
#include <system_error>

#include <chrono>
#include <utility>

namespace engine
{
namespace
{
std::uint64_t render_time()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}
} // namespace
SceneWorkState RenderSystem::prepare_scope(SceneScopeId id, const SceneWorkBudget& budget,
                                           Camera camera)
{
    if (!is_initialized() || !std::isfinite(budget.seconds) || budget.seconds <= 0 ||
        budget.items == 0 || budget.upload_bytes == 0)
    {
        return SceneWorkState::failed;
    }
    const bool current = id == SceneScopeId{};
    auto* candidate = world_ ? (current ? world_->scene() : world_->scope_candidate(id)) : nullptr;
    if (current && !candidate)
    {
        auto& snapshot = scope_preparations_[id].snapshot;
        snapshot.scope = id;
        RenderPrepareProgress progress;
        const auto state = prepare_resources(progress, budget.seconds, camera);
        snapshot.state = state == RenderPrepareStatus::ready    ? SceneWorkState::not_required
                         : state == RenderPrepareStatus::failed ? SceneWorkState::failed
                                                                : SceneWorkState::working;
        return snapshot.state;
    }
    if (!candidate || !std::isfinite(budget.seconds) || budget.seconds <= 0 || budget.items == 0)
    {
        return SceneWorkState::failed;
    }
    auto& record = scope_preparations_[id];
    record.snapshot.scope = id;
    if (record.started_ns == 0)
    {
        record.started_ns = render_time();
    }
    const auto started_ns = record.started_ns;
    const auto original = std::exchange(scope_preparing_scene_, candidate);
    RenderPrepareProgress progress;
    const auto previous_config = config_;
    config_.prepare_items_per_step = std::min(config_.prepare_items_per_step, budget.items);
    config_.upload_bytes_per_step = std::min(config_.upload_bytes_per_step, budget.upload_bytes);
    const auto status = prepare_resources(progress, budget.seconds, camera);
    config_ = previous_config;
    scope_preparing_scene_ = original;
    if (record.started_ns == 0)
    {
        record.started_ns = started_ns;
    }
    SceneWorkProgress work;
    work.domain = "render";
    work.stage = "preparing";
    work.completed = progress.resources_ready;
    work.total = progress.resources_total;
    work.total_known = progress.discovery_complete;
    work.state = status == RenderPrepareStatus::ready    ? SceneWorkState::ready
                 : status == RenderPrepareStatus::failed ? SceneWorkState::failed
                                                         : SceneWorkState::working;
    if (work.state == SceneWorkState::failed)
    {
        work.error_code = "render_preparation_failed";
        work.error = preparation_ ? preparation_->error : "Render preparation failed";
    }
    const auto previous = record.snapshot.stages;
    if (previous.empty() || previous.front().completed != work.completed ||
        previous.front().state != work.state || record.completed != progress.upload_bytes)
    {
        work.progress_revision = previous.empty() ? 1 : previous.front().progress_revision + 1;
        work.last_progress_ns = render_time();
    }
    else
    {
        work.progress_revision = previous.front().progress_revision;
        work.last_progress_ns = previous.front().last_progress_ns;
    }
    work.elapsed_ns = render_time() - record.started_ns;
    record.completed = progress.upload_bytes;
    record.snapshot.state = work.state;
    record.snapshot.staging_bytes = progress.staging_bytes;
    record.snapshot.stages = {std::move(work)};
    if (preparation_)
    {
        auto add = [&](std::string stage, std::uint64_t completed, std::uint64_t total, bool known,
                       SceneWorkState state, std::uint32_t flight = 0)
        {
            SceneWorkProgress value;
            value.domain = "render";
            value.stage = std::move(stage);
            value.completed = completed;
            value.total = total;
            value.total_known = known;
            value.state = state;
            value.in_flight = flight;
            value.elapsed_ns = render_time() - record.started_ns;
            const auto old = std::find_if(previous.begin(), previous.end(), [&](const auto& item)
                                          { return item.stage == value.stage; });
            const bool changed = old == previous.end() || old->completed != value.completed ||
                                 old->total != value.total ||
                                 old->total_known != value.total_known ||
                                 old->state != value.state || old->in_flight != value.in_flight;
            value.progress_revision = old == previous.end() ? 1 : old->progress_revision + changed;
            value.last_progress_ns = changed ? render_time() : old->last_progress_ns;
            record.snapshot.stages.push_back(std::move(value));
        };
        add("discovering", progress.instances_ready, progress.instances_total,
            progress.discovery_complete,
            progress.discovery_complete ? SceneWorkState::ready : SceneWorkState::working);
        add("decoding", progress.decoded_blocks, progress.decoded_blocks,
            record.snapshot.state == SceneWorkState::ready,
            record.snapshot.state == SceneWorkState::ready ? SceneWorkState::ready
                                                           : SceneWorkState::working,
            progress.texture_jobs);
        add("uploading", progress.upload_bytes, progress.upload_total,
            record.snapshot.state == SceneWorkState::ready,
            record.snapshot.state == SceneWorkState::ready ? SceneWorkState::ready
                                                           : SceneWorkState::working);
        add("programs", progress.programs_ready, progress.programs_total,
            progress.discovery_complete,
            progress.programs_ready == progress.programs_total ? SceneWorkState::ready
                                                               : SceneWorkState::working);
        const auto stages = scene_resources_ && scene_resources_->selected_targets
                                ? scene_resources_->targets().stage
                                : 0;
        add("targets", stages, 10, true,
            stages == 10 ? SceneWorkState::ready : SceneWorkState::working);
    }
    if (record.snapshot.state == SceneWorkState::ready)
    {
        invoking_extensions_ = true;
        for (auto* extension : extensions_)
        {
            const auto state = extension->prepare_scope(
                current ? SceneScopeId{world_->epoch(), candidate->id()} : id, budget);
            ScenePreparationSnapshot value;
            if (extension->scope_status(
                    current ? SceneScopeId{world_->epoch(), candidate->id()} : id, value))
            {
                record.snapshot.stages.insert(record.snapshot.stages.end(), value.stages.begin(),
                                              value.stages.end());
            }
            if (state != SceneWorkState::ready && state != SceneWorkState::not_required)
            {
                record.snapshot.state = state;
            }
        }
        invoking_extensions_ = false;
    }
    return record.snapshot.state;
}

bool RenderSystem::scope_status(SceneScopeId id, ScenePreparationSnapshot& output) const
{
    output = {};
    const auto found = scope_preparations_.find(id);
    if (found == scope_preparations_.end() ||
        (id != SceneScopeId{} && (!world_ || !world_->scope_candidate(id))))
    {
        return false;
    }
    output = found->second.snapshot;
    return true;
}

void RenderSystem::release_scope(SceneScopeId id) noexcept
{
    for (auto* extension : extensions_)
    {
        extension->release_scope(id);
    }
    if (preparation_ && world_)
    {
        auto* scene = world_->scope_candidate(id);
        if (scene && scene != world_->scene() && scene_resources_)
        {
            for (const auto& mesh : scene_resources_->meshes)
            {
                if (scene->find_node(mesh.node_id))
                {
                    preparation_->dirty_nodes.insert(mesh.node_id);
                }
            }
        }
    }
    scope_preparations_.erase(id);
    scope_frames_.erase(id);
}

RenderFrameSnapshot RenderSystem::submitted_frame(RenderFrameKind kind) const
{
    RenderFrameSnapshot frame;
    frame.device_generation = device_generation_;
    frame.submission_id = submission_sequence_ + 1;
    frame.kind = kind;
    frame.submitted_at_ns = render_time();
    if (world_ && world_->native_world())
    {
        frame.world_epoch = world_->epoch();
        frame.scene_id = world_->scene() ? world_->scene()->id() : 0;
        auto query = flecs::world(world_->native_world()).query<const ecs_detail::ScopeState>();
        query.each(
            [&](const ecs_detail::ScopeState& state)
            {
                if (state.scope == frame.scene_id)
                {
                    frame.content_revision = state.content_revision;
                }
            });
    }
    frame.ui_draws = render_detail::ui_draws;
    if (scene_resources_ && kind == RenderFrameKind::scene)
    {
        for (const auto& view : scene_resources_->view_stats)
        {
            frame.mesh_draws += view.draw_calls;
        }
    }
    return frame;
}

void RenderSystem::record_frame(RenderFrameKind kind)
{
    last_frame_ = submitted_frame(kind);
    ++submission_sequence_;
    if (world_ && world_->native_world() && kind == RenderFrameKind::scene && scene_collected_)
    {
        auto query = flecs::world(world_->native_world()).query<const ecs_detail::ScopeState>();
        query.each(
            [&](flecs::entity entity, const ecs_detail::ScopeState& state)
            {
                const SceneScopeId scope{world_->epoch(), state.scope};
                if (state.published && entity.has<ecs_detail::Published>() &&
                    world_->scope_candidate(scope) == world_->scene() &&
                    state.publication_revision <= last_frame_.content_revision)
                {
                    scope_frames_[scope] = last_frame_;
                }
            });
        std::erase_if(scope_frames_, [&](const auto& entry)
                      { return world_->scope_candidate(entry.first) != world_->scene(); });
    }
    advance_screenshots();
}

bool RenderSystem::frame_status(SceneScopeId scope, RenderFrameSnapshot& output) const
{
    output = {};
    if (!renderer_initialized_ || last_frame_.submission_id == 0 ||
        last_frame_.device_generation != device_generation_)
    {
        return false;
    }
    if (scope == SceneScopeId{})
    {
        output = last_frame_;
        return true;
    }
    const auto found = scope_frames_.find(scope);
    if (!world_ || found == scope_frames_.end() ||
        world_->scope_candidate(scope) != world_->scene() ||
        found->second.device_generation != device_generation_)
    {
        return false;
    }
    output = found->second;
    return true;
}

bool RenderSystem::request_screenshot(const ScreenshotRequest& request, ScreenshotId& output)
{
    output = {};
    const auto path = PathUtils::from_utf8(request.path);
    std::error_code error;
    if (!is_initialized() || !renderer_initialized_ || !SDL_IsMainThread() ||
        screenshots_.size() >= 64 || request.path.find('\0') != std::string::npos ||
        !path.is_absolute() || path.extension() != ".png" ||
        !std::filesystem::is_directory(path.parent_path(), error) || error ||
        std::filesystem::exists(path, error) || error || screenshot_sequence_ == UINT64_MAX ||
        (request.selection == ScreenshotSelection::scene_scope &&
         (!world_ || !world_->scope_candidate(request.scope))))
    {
        return false;
    }
    for (const auto& [key, operation] : screenshots_)
    {
        (void)key;
        if (operation.request.path == request.path)
        {
            return false;
        }
    }
    if (screenshot_owner_ == 0)
    {
        static std::atomic<std::uint64_t> owners{0};
        auto previous = owners.load();
        do
        {
            if (previous == UINT64_MAX)
            {
                return false;
            }
        } while (!owners.compare_exchange_weak(previous, previous + 1));
        screenshot_owner_ = previous + 1;
    }
    if (request.device_generation != 0 && request.device_generation != device_generation_)
    {
        return false;
    }
    ScreenshotOperation operation;
    operation.request = request;
    if (operation.request.device_generation == 0)
    {
        operation.request.device_generation = device_generation_;
        operation.request.after_submission_id = submission_sequence_;
    }
    operation.snapshot.id = {screenshot_owner_, ++screenshot_sequence_};
    operation.snapshot.path = request.path;
    operation.temporary_path =
        request.path + ".capture-" + std::to_string(screenshot_sequence_) + ".png";
    if (std::filesystem::exists(PathUtils::from_utf8(operation.temporary_path), error) || error)
    {
        return false;
    }
    output = operation.snapshot.id;
    screenshots_.emplace(output.value, std::move(operation));
    return true;
}

bool RenderSystem::screenshot_status(ScreenshotId id, ScreenshotSnapshot& output) const
{
    output = {};
    const auto found = screenshots_.find(id.value);
    if (id.owner != screenshot_owner_ || found == screenshots_.end() ||
        found->second.release_requested)
    {
        return false;
    }
    output = found->second.snapshot;
    return true;
}

bool RenderSystem::cancel_screenshot(ScreenshotId id)
{
    const auto found = screenshots_.find(id.value);
    if (id.owner != screenshot_owner_ || found == screenshots_.end())
    {
        return false;
    }
    auto& state = found->second.snapshot.state;
    if (state == ScreenshotState::complete || state == ScreenshotState::failed)
    {
        return false;
    }
    state = ScreenshotState::cancelled;
    return true;
}

bool RenderSystem::release_screenshot(ScreenshotId id)
{
    const auto found = screenshots_.find(id.value);
    if (id.owner != screenshot_owner_ || found == screenshots_.end())
    {
        return false;
    }
    const auto state = found->second.snapshot.state;
    if (state != ScreenshotState::complete && state != ScreenshotState::failed &&
        state != ScreenshotState::cancelled)
    {
        return false;
    }
    if (active_screenshot_ == id.value)
    {
        found->second.release_requested = true;
    }
    else
    {
        screenshots_.erase(found);
    }
    return true;
}

void RenderSystem::submit_screenshot(RenderFrameKind kind)
{
    if (active_screenshot_ || !frame_output_)
    {
        return;
    }
    for (auto& [key, operation] : screenshots_)
    {
        if (operation.snapshot.state != ScreenshotState::waiting_frame)
        {
            continue;
        }
        if (operation.request.device_generation != device_generation_)
        {
            operation.snapshot.state = ScreenshotState::failed;
            operation.snapshot.error_code = "device_changed";
            operation.snapshot.error = "Rendering device changed before capture";
            continue;
        }
        if (submission_sequence_ + 1 <= operation.request.after_submission_id)
        {
            continue;
        }
        const auto* scene = world_ ? world_->scene() : nullptr;
        if (operation.request.selection == ScreenshotSelection::scene_scope)
        {
            auto* target = world_ ? world_->scope_candidate(operation.request.scope) : nullptr;
            if (!target)
            {
                operation.snapshot.state = ScreenshotState::failed;
                operation.snapshot.error_code = "scope_retired";
                operation.snapshot.error = "Capture target no longer exists";
                continue;
            }
            if (kind != RenderFrameKind::scene || target != scene || !scene_collected_)
            {
                continue;
            }
        }
        if (!frame_output_->capture())
        {
            operation.snapshot.state = ScreenshotState::failed;
            operation.snapshot.error_code = "readback_unavailable";
            operation.snapshot.error = "Final display texture cannot be read back";
            continue;
        }
        operation.snapshot.frame = submitted_frame(kind);
        operation.snapshot.state = ScreenshotState::reading_back;
        active_screenshot_ = key;
        {
            std::lock_guard lock(callbacks_->mutex);
            callbacks_->pending_path = operation.temporary_path;
            callbacks_->readback_received = false;
            callbacks_->status.store(ScreenshotStatus::pending);
        }
        break;
    }
}

void RenderSystem::advance_screenshots()
{
    const auto found = screenshots_.find(active_screenshot_);
    if (!active_screenshot_ || found == screenshots_.end() || !frame_output_ || !callbacks_)
    {
        return;
    }
    auto& operation = found->second;
    if (frame_output_->collect(frame_number_))
    {
        const auto& pixels = frame_output_->pixels();
        callbacks_->screenShot(operation.temporary_path.c_str(), frame_output_->capture_width(),
                               frame_output_->capture_height(), frame_output_->capture_width() * 4,
                               bgfx::TextureFormat::BGRA8, pixels.data(),
                               static_cast<std::uint32_t>(pixels.size()),
                               bgfx::getCaps()->originBottomLeft);
        callbacks_->process(tasks_);
        if (operation.snapshot.state != ScreenshotState::cancelled)
        {
            operation.snapshot.state = ScreenshotState::writing;
        }
    }
    const auto status = callbacks_->status.load();
    if (status != ScreenshotStatus::complete && status != ScreenshotStatus::failed)
    {
        return;
    }
    std::error_code error;
    if (operation.snapshot.state == ScreenshotState::cancelled ||
        status == ScreenshotStatus::failed)
    {
        std::filesystem::remove(PathUtils::from_utf8(operation.temporary_path), error);
        if (operation.snapshot.state != ScreenshotState::cancelled)
        {
            operation.snapshot.state = ScreenshotState::failed;
            operation.snapshot.error_code = "write_failed";
            operation.snapshot.error = "Cannot encode or write capture";
        }
    }
    else if (!std::filesystem::exists(PathUtils::from_utf8(operation.request.path), error) &&
             !error &&
             SDL_RenamePath(operation.temporary_path.c_str(), operation.request.path.c_str()))
    {
        operation.snapshot.state = ScreenshotState::complete;
    }
    else
    {
        operation.snapshot.state = ScreenshotState::failed;
        operation.snapshot.error_code = "publish_failed";
        operation.snapshot.error = "Cannot publish capture file without replacing existing data";
    }
    const bool release = operation.release_requested;
    const auto id = active_screenshot_;
    active_screenshot_ = 0;
    if (release)
    {
        screenshots_.erase(id);
    }
}
} // namespace engine
