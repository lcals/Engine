#include "runtime/animation/animation_system.hpp"

#include "runtime/scene/animator_runtime.hpp"
#include "runtime/scene/bone_attachment_runtime.hpp"

#include "runtime/animation/animation_attachment.hpp"
#include "runtime/animation/animation_clip.hpp"
#include "runtime/animation/animation_event_interval.hpp"
#include "runtime/animation/animation_ik.hpp"
#include "runtime/animation/animation_services.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/resource/resource_reference.hpp"
#include "runtime/resource/types/animation_resource.hpp"
#include "runtime/resource/types/animation_retarget_profile_resource.hpp"
#include "runtime/resource/types/skeleton_resource.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/world/world_system.hpp"

#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace engine
{
namespace
{
constexpr std::size_t maximum_events_per_tick = 4096;
bool active_transform_chain(Node node)
{
    for (auto current = node; current; current = current.parent())
    {
        const auto transform = current.get_component<Transform>();
        if (!transform || !transform.active())
        {
            return false;
        }
    }
    return true;
}

struct AnimationSampler
{
    std::shared_ptr<const animation_detail::CompiledAnimation> clip;
    std::unique_ptr<ozz::animation::SamplingJob::Context> context;
    std::vector<ozz::math::SoaTransform> sampled_locals, locals;
    std::vector<schema::JointLocalPose> source_pose, target_pose;
    void prepare(std::shared_ptr<const animation_detail::CompiledAnimation> compiled)
    {
        clip = std::move(compiled);
        const auto& skeleton = *clip->sampling->skeleton;
        context = std::make_unique<ozz::animation::SamplingJob::Context>(skeleton.num_joints());
        sampled_locals.resize(skeleton.num_soa_joints());
        locals.resize(clip->target->skeleton->num_soa_joints());
        source_pose.resize(clip->sampling->order.size());
        target_pose.resize(clip->target->order.size());
    }
    bool sample(double seconds)
    {
        ozz::animation::SamplingJob job;
        job.animation = clip->animation.get();
        job.context = context.get();
        job.ratio = static_cast<float>(seconds / clip->animation->duration());
        job.output = ozz::make_span(sampled_locals);
        if (!job.Run())
        {
            return false;
        }
        if (!clip->retarget)
        {
            std::fill(locals.begin(), locals.end(), ozz::math::SoaTransform::identity());
            for (std::size_t dfs = 0; dfs < clip->target->order.size(); ++dfs)
            {
                const auto source_dfs = clip->source_dfs[dfs];
                const auto pose =
                    animation_detail::local_pose(sampled_locals[source_dfs / 4], source_dfs % 4);
                const auto rest = animation_detail::local_pose(
                    clip->target->skeleton->joint_rest_poses()[dfs / 4], dfs % 4);
                const auto channels = clip->channels[clip->sampling->order[source_dfs]];
                const schema::JointLocalPose mapped(
                    channels & 1 ? pose.translation() : rest.translation(),
                    channels & 2 ? pose.rotation() : rest.rotation(),
                    channels & 4 ? pose.scale() : rest.scale());
                animation_detail::set_local_pose(locals[dfs / 4], dfs % 4, mapped);
            }
            return true;
        }
        for (std::size_t dfs = 0; dfs < clip->sampling->order.size(); ++dfs)
        {
            source_pose[clip->sampling->order[dfs]] =
                animation_detail::local_pose(sampled_locals[dfs / 4], dfs % 4);
        }
        if (!clip->retarget->retarget(source_pose, target_pose))
        {
            return false;
        }
        std::fill(locals.begin(), locals.end(), ozz::math::SoaTransform::identity());
        for (std::size_t dfs = 0; dfs < clip->target->order.size(); ++dfs)
        {
            const auto& pose = target_pose[clip->target->order[dfs]];
            if (!model_value_utils::vector(pose.translation()) ||
                !model_value_utils::scale(pose.scale()) ||
                !model_value_utils::quaternion(pose.rotation()))
            {
                return false;
            }
            animation_detail::set_local_pose(locals[dfs / 4], dfs % 4, pose);
        }
        return true;
    }
};
struct AnimationEntry
{
    AnimationSampler current, previous;
    std::shared_ptr<const animation_detail::AnimationPose> frozen;
    std::vector<ozz::math::SoaTransform> locals, committed_locals;
    std::vector<ozz::math::Float4x4> models;
    std::vector<std::array<float, 16>> palette, normal_palette;
    ozz::math::Float4x4 mesh_to_skeleton, skeleton_to_mesh;
    std::uint64_t mesh_revision = 0, mesh_instance = 0, animator_instance = 0;
    std::uint64_t deformation_revision = 0;
    bool blended = false, sampled = false;
    bool prepare(const schema::MeshDataT& mesh, MeshRenderer renderer, Animator animator,
                 std::shared_ptr<const animation_detail::CompiledAnimation> clip)
    {
        if (!animation_detail::matrix(mesh.mesh_to_skeleton, mesh_to_skeleton) ||
            !animation_detail::inverse(mesh_to_skeleton, skeleton_to_mesh))
        {
            return false;
        }
        current.prepare(std::move(clip));
        const auto& skeleton = *current.clip->target->skeleton;
        locals.resize(skeleton.num_soa_joints());
        models.resize(skeleton.num_joints());
        palette.resize(models.size());
        normal_palette.resize(models.size());
        mesh_revision = renderer.mesh_revision();
        mesh_instance = renderer.instance_id();
        animator_instance = animator.instance_id();
        return true;
    }
    bool sample(double seconds, double previous_seconds, double blend, bool rest, bool frozen_final,
                std::span<const animation_detail::IkOperation> ik)
    {
        const auto& skeleton = *current.clip->target;
        if (frozen_final && sampled)
        {
            locals = committed_locals;
        }
        else if (rest)
        {
            const auto poses = skeleton.skeleton->joint_rest_poses();
            locals.assign(poses.begin(), poses.end());
        }
        else
        {
            if (!current.sample(seconds))
            {
                return false;
            }
            if (blend < 1.0)
            {
                ozz::span<const ozz::math::SoaTransform> from;
                if (frozen)
                {
                    from = ozz::make_span(frozen->locals);
                }
                else if (previous.clip)
                {
                    if (!previous.sample(previous_seconds))
                    {
                        return false;
                    }
                    from = ozz::make_span(previous.locals);
                }
                else
                {
                    from = skeleton.skeleton->joint_rest_poses();
                }
                std::array<ozz::animation::BlendingJob::Layer, 2> layers;
                layers[0].transform = from;
                layers[0].weight = static_cast<float>(1.0 - blend);
                layers[1].transform = ozz::make_span(current.locals);
                layers[1].weight = static_cast<float>(blend);
                ozz::animation::BlendingJob job;
                job.layers = ozz::make_span(layers);
                job.rest_pose = skeleton.skeleton->joint_rest_poses();
                job.output = ozz::make_span(locals);
                if (!job.Run())
                {
                    return false;
                }
            }
            else
            {
                locals = current.locals;
            }
        }
        ozz::animation::LocalToModelJob fk;
        fk.skeleton = skeleton.skeleton.get();
        fk.input = ozz::make_span(locals);
        fk.output = ozz::make_span(models);
        if (!fk.Run() ||
            (!rest && !frozen_final && !animation_detail::apply_ik(skeleton, ik, locals, models)))
        {
            return false;
        }
        for (std::size_t dfs = 0; dfs < models.size(); ++dfs)
        {
            const auto index = skeleton.order[dfs];
            const auto transform =
                skeleton_to_mesh * models[dfs] * skeleton.inverse_bind[index] * mesh_to_skeleton;
            ozz::math::Float4x4 inverse;
            if (!animation_detail::inverse(transform, inverse))
            {
                return false;
            }
            animation_detail::store_matrix(transform, palette[index]);
            animation_detail::store_matrix(ozz::math::Transpose(inverse), normal_palette[index]);
            auto& normal = normal_palette[index];
            normal[3] = normal[7] = normal[11] = 0;
            normal[12] = normal[13] = normal[14] = 0;
            normal[15] = 1;
        }
        return true;
    }
};
struct AnimationSample
{
    AnimationEntry* entry = nullptr;
    Animator animator;
    MeshRenderer renderer;
    double seconds = 0, previous_seconds = 0, blend = 1, fade_elapsed = 0;
    AnimationPlayback playback = AnimationPlayback::stopped;
    std::uint64_t cycle = 0;
    bool completed = false, rest = false, frozen_final = false, event_starting = false;
    std::vector<animation_detail::IkOperation> ik;
    std::vector<AnimationEvent> events;
};
bool sample_range(std::vector<AnimationSample>& samples, std::size_t begin, std::size_t end)
{
    ENGINE_PROFILE_ZONE("Animation.sample_batch");
    for (std::size_t index = begin; index < end; ++index)
    {
        auto& sample = samples[index];
        if (!sample.entry->sample(sample.seconds, sample.previous_seconds, sample.blend,
                                  sample.rest, sample.frozen_final, sample.ik))
        {
            return false;
        }
    }
    return true;
}
bool stage_time(AnimationSample& sample, double delta_time, const Animator::Data& settings,
                std::size_t& event_count, bool tick_enabled)
{
    const auto* state = engine::ecs_detail::component_runtime(sample.animator);
    const auto* animation = state->animation_.data<AnimationResource>();
    const double duration = animation->duration;
    sample.seconds = state->time_;
    sample.previous_seconds = state->previous_time_;
    sample.playback = state->playback_;
    sample.completed = state->completed_;
    sample.cycle = state->cycle_;
    sample.fade_elapsed = state->fade_elapsed_;
    sample.rest = sample.playback == AnimationPlayback::stopped;
    sample.event_starting = state->event_starting_;
    const bool running = sample.playback == AnimationPlayback::playing && tick_enabled;
    if (running)
    {
        const double unwrapped = state->time_ + delta_time * settings.speed;
        if (!std::isfinite(unwrapped))
        {
            return false;
        }
        if (settings.loop && delta_time > 0 && settings.speed > 0)
        {
            double periods = std::floor(unwrapped / duration);
            sample.seconds = std::fma(-periods, duration, unwrapped);
            if (sample.seconds < 0)
            {
                periods -= 1;
                sample.seconds = std::fma(-periods, duration, unwrapped);
            }
            else if (sample.seconds >= duration)
            {
                periods += 1;
                sample.seconds = std::fma(-periods, duration, unwrapped);
            }
            if (periods < 0 || periods >= 0x1p53 || sample.seconds < 0 ||
                sample.seconds >= duration ||
                static_cast<std::uint64_t>(periods) >
                    std::numeric_limits<std::uint64_t>::max() - sample.cycle)
            {
                return false;
            }
            sample.cycle += static_cast<std::uint64_t>(periods);
        }
        else
        {
            sample.seconds = std::min(unwrapped, duration);
        }
        if (state->fade_seconds_ > 0)
        {
            sample.fade_elapsed +=
                std::min(delta_time, state->fade_seconds_ - state->fade_elapsed_);
            if (state->previous_advancing_ && state->previous_animation_)
            {
                const auto previous_duration =
                    state->previous_animation_.data<AnimationResource>()->duration;
                const double previous =
                    sample.previous_seconds + delta_time * state->previous_speed_;
                if (!std::isfinite(previous))
                {
                    return false;
                }
                sample.previous_seconds =
                    state->previous_loop_
                        ? std::fmod(previous, previous_duration)
                        : std::min(previous, static_cast<double>(previous_duration));
            }
        }
        if (!settings.loop && sample.seconds >= duration && settings.speed > 0 && delta_time > 0 &&
            sample.fade_elapsed >= state->fade_seconds_)
        {
            sample.playback = AnimationPlayback::paused;
            sample.completed = true;
        }
        if (settings.speed > 0 && (delta_time > 0 || state->event_starting_))
        {
            std::vector<const schema::AnimationEventMarkerDataT*> markers;
            std::vector<double> times;
            for (const auto& marker : animation->events)
            {
                if (!marker)
                {
                    return false;
                }
                markers.push_back(marker.get());
                times.push_back(marker->time);
            }
            for (const auto& marker : state->event_markers_)
            {
                markers.push_back(&marker);
                times.push_back(marker.time);
            }
            std::vector<animation_detail::EventCrossing> crossings;
            if (!animation_detail::collect_event_crossings(
                    times, duration, state->time_, settings.loop ? unwrapped : sample.seconds,
                    settings.loop, state->event_starting_, maximum_events_per_tick - event_count,
                    crossings))
            {
                return false;
            }
            sample.event_starting = false;
            for (const auto& [cycle, index] : crossings)
            {
                if (cycle > std::numeric_limits<std::uint64_t>::max() - state->cycle_)
                {
                    return false;
                }
                AnimationEvent event;
                event.scene_id = sample.animator.node().scene()->id();
                event.node_id = sample.animator.node().id();
                event.component_instance_id = sample.animator.instance_id();
                event.playback_id = state->playback_id_;
                event.animation_guid = state->animation_->info().guid;
                event.resource_generation = state->animation_->generation();
                event.track_revision = state->track_revision_;
                event.cycle = state->cycle_ + cycle;
                event.marker = *markers[index];
                sample.events.push_back(std::move(event));
            }
            event_count += sample.events.size();
        }
    }
    sample.blend = state->fade_seconds_ > 0 ? sample.fade_elapsed / state->fade_seconds_ : 1;
    return true;
}
void resolve_pending_attachments(WorldSystem& world)
{
    auto query = flecs::world(world.native_world())
                     .query_builder<const ecs_detail::ComponentStorage<BoneAttachment::Data>>()
                     .with<ecs_detail::Published>()
                     .build();
    query.each(
        [&](flecs::entity entity, const auto& stored)
        {
            auto attachment = world.find_node(entity.id()).get_component<BoneAttachment>();
            if (!attachment || !engine::ecs_detail::component_runtime(attachment)->binding_pending)
            {
                return;
            }
            auto* runtime = engine::ecs_detail::component_runtime(attachment);
            runtime->binding_pending = false;
            (void)animation_detail::resolve_attachment_source(
                attachment, stored.data, runtime->source, runtime->resolved_socket_index,
                runtime->skeleton_generation);
        });
}
struct AttachmentCommit
{
    BoneAttachment attachment;
    bool following = false;
    std::array<float, 16> local;
    std::uint64_t revision = 0, generation = 0;
};
bool candidate_world(Node node, const std::map<std::uint64_t, ozz::math::Float4x4>& overrides,
                     ozz::math::Float4x4& output)
{
    output = ozz::math::Float4x4::identity();
    std::vector<Node> chain;
    for (auto current = node; current; current = current.parent())
    {
        const auto found = overrides.find(current.id());
        if (found != overrides.end())
        {
            output = found->second;
            break;
        }
        chain.push_back(current);
    }
    for (auto current = chain.rbegin(); current != chain.rend(); ++current)
    {
        const auto transform = current->get_component<Transform>();
        TransformSettings pose;
        if (!transform.get_settings(pose))
        {
            return false;
        }
        output = output * animation_detail::authored_matrix(pose);
    }
    return true;
}
bool publish_attachments(WorldSystem& world)
{
    std::vector<BoneAttachment> attachments;
    auto query = flecs::world(world.native_world())
                     .query_builder<const ecs_detail::ComponentStorage<BoneAttachment::Data>>()
                     .with<ecs_detail::Published>()
                     .build();
    query.each(
        [&](flecs::entity entity, const auto&)
        { attachments.push_back(world.find_node(entity.id()).get_component<BoneAttachment>()); });
    const auto depth = [](Node node)
    {
        std::size_t value = 0;
        for (auto parent = node.parent(); parent; parent = parent.parent())
        {
            ++value;
        }
        return value;
    };
    std::stable_sort(attachments.begin(), attachments.end(), [&](const auto& a, const auto& b)
                     { return depth(a.node()) < depth(b.node()); });
    std::vector<AttachmentCommit> commits;
    std::map<std::uint64_t, ozz::math::Float4x4> overrides;
    for (auto attachment : attachments)
    {
        AttachmentCommit commit;
        commit.attachment = attachment;
        BoneAttachment::Data data;
        if (!attachment.get_settings(data))
        {
            return false;
        }
        const auto source = engine::ecs_detail::component_runtime(attachment)->source;
        AnimationPoseTransform socket;
        if (attachment.active() && data.follow_enabled && source && source.active() &&
            animation_detail::attachment_can_drive(attachment.node()) &&
            source.get_socket_pose(data.socket_name, socket))
        {
            const auto desired = animation_detail::load_matrix(socket.world) *
                                 animation_detail::pose_matrix(data.offset);
            ozz::math::Float4x4 parent_world, parent_inverse;
            if (!candidate_world(attachment.node().parent(), overrides, parent_world) ||
                !animation_detail::invert_matrix(parent_world, parent_inverse))
            {
                return false;
            }
            const auto local = parent_inverse * desired;
            TransformSettings exact;
            if (!animation_detail::decompose_authored(local, exact))
            {
                return false;
            }
            animation_detail::store_matrix(local, commit.local);
            commit.following = true;
            commit.revision = socket.pose_revision;
            commit.generation =
                engine::ecs_detail::component_runtime(source)->target_skeleton_->generation();
            overrides.emplace(attachment.node().id(), desired);
        }
        commits.push_back(commit);
    }
    // 所有矩阵验证完成后再提交，避免失败时只更新一部分挂件。
    for (const auto& commit : commits)
    {
        auto attachment = commit.attachment;
        if (!attachment)
        {
            return false;
        }
        auto entity = flecs::world(world.native_world()).entity(attachment.node().id());
        if (commit.following)
        {
            entity.add<ecs_detail::TransformDrive>();
            auto* drive = entity.try_get_mut<ecs_detail::TransformDrive>();
            drive->owner_type = BoneAttachment::component_type;
            drive->owner_instance_id = attachment.instance_id();
            drive->validate = animation_detail::attachment_drive_valid;
            drive->local = commit.local;
            drive->active = true;
            engine::ecs_detail::component_runtime(attachment)->following = true;
            engine::ecs_detail::component_runtime(attachment)->source_pose_revision =
                commit.revision;
            engine::ecs_detail::component_runtime(attachment)->skeleton_generation =
                commit.generation;
        }
        else
        {
            animation_detail::clear_attachment_drive(attachment);
        }
    }
    if (!commits.empty())
    {
        world.invalidate_scene_state();
    }
    return world.refresh_scene_state();
}
} // namespace
struct AnimationSystem::State
{
    std::map<animation_detail::ResourceIdentity,
             std::shared_ptr<const animation_detail::CompiledSkeleton>>
        skeletons;
    std::map<animation_detail::AnimationIdentity,
             std::shared_ptr<const animation_detail::CompiledAnimation>>
        clips;
    std::map<std::pair<animation_detail::ResourceIdentity, animation_detail::ResourceIdentity>,
             std::shared_ptr<const ozz::animation::Animation>>
        sampling_clips;
    std::map<std::uint64_t, std::unique_ptr<AnimationEntry>> entries;
    std::vector<AnimationSample> samples;
    bool skeleton(const ResourceReference& resource,
                  std::shared_ptr<const animation_detail::CompiledSkeleton>& output)
    {
        const auto key = animation_detail::identity(*resource);
        if (auto found = skeletons.find(key); found != skeletons.end())
        {
            output = found->second;
            return true;
        }
        auto candidate = std::make_shared<animation_detail::CompiledSkeleton>();
        if (!candidate->prepare(resource))
        {
            return false;
        }
        output = candidate;
        skeletons.emplace(key, std::move(candidate));
        return true;
    }
    bool compile(ResourceLoader* resources, ResourceHandle mesh_resource,
                 const ResourceReference& clip, const schema::MeshDataT& mesh,
                 const ResourceReference& profile,
                 const std::shared_ptr<const AnimationRetargetBinding>& binding,
                 const ResourceReference& bound_source,
                 std::shared_ptr<const animation_detail::CompiledAnimation>& output)
    {
        output.reset();
        ResourceReference target;
        ResourceReference source;
        if (bound_source && !source.retain(bound_source))
        {
            return false;
        }
        if (!resources || !clip || !clip.data<AnimationResource>() ||
            !target.dependency(resources, mesh_resource, mesh.skeleton_guid) || !target ||
            !target.data<SkeletonResource>() ||
            (!source && !source.dependency(resources, clip.handle(),
                                           clip.data<AnimationResource>()->skeleton_guid)) ||
            !source || !source.data<SkeletonResource>())
        {
            return false;
        }
        schema::AnimationDataT rest;
        rest.schema_version = 2;
        if (binding ? (!profile || !profile.data<AnimationRetargetResource>() ||
                       profile.data<AnimationRetargetResource>()->source_skeleton_guid !=
                           source->info().guid ||
                       profile.data<AnimationRetargetResource>()->target_skeleton_guid !=
                           target->info().guid ||
                       !animation_detail::valid_binding(mesh, *target.data<SkeletonResource>(),
                                                        *target.data<SkeletonResource>(), rest))
                    : !animation_detail::valid_binding(mesh, *target.data<SkeletonResource>(),
                                                       *source.data<SkeletonResource>(),
                                                       *clip.data<AnimationResource>()))
        {
            return false;
        }
        const animation_detail::AnimationIdentity key{
            animation_detail::identity(*clip), animation_detail::identity(*source),
            animation_detail::identity(*target),
            profile ? animation_detail::identity(*profile) : animation_detail::ResourceIdentity{}};
        if (const auto found = clips.find(key); found != clips.end())
        {
            output = found->second;
            return true;
        }
        std::shared_ptr<const animation_detail::CompiledSkeleton> target_compiled, source_compiled;
        if (!skeleton(target, target_compiled) || !skeleton(source, source_compiled))
        {
            return false;
        }
        const auto sampling_key =
            std::pair(animation_detail::identity(*clip), animation_detail::identity(*source));
        auto found_sampling = sampling_clips.find(sampling_key);
        if (found_sampling == sampling_clips.end())
        {
            ozz::unique_ptr<ozz::animation::Animation> compiled;
            const auto* data = clip.data<AnimationResource>();
            if (data->sampling_encoding == schema::SamplingEncoding::ozz_archive)
            {
                compiled = ozz::make_unique<ozz::animation::Animation>();
                if (!animation_detail::read_sampling(data->sampling_archive, *compiled))
                {
                    return false;
                }
            }
            else if (!animation_detail::build_animation(*data, *source_compiled->data,
                                                        source_compiled->order, compiled))
            {
                return false;
            }
            if (compiled->num_tracks() != source_compiled->skeleton->num_joints())
            {
                return false;
            }
            std::shared_ptr<const ozz::animation::Animation> shared(std::move(compiled));
            found_sampling = sampling_clips.emplace(sampling_key, std::move(shared)).first;
        }
        auto candidate = std::make_shared<animation_detail::CompiledAnimation>();
        if (!candidate->prepare(clip, source, target_compiled, source_compiled, binding,
                                found_sampling->second))
        {
            return false;
        }
        output = candidate;
        clips.emplace(key, std::move(candidate));
        return true;
    }
};
AnimationSystem::AnimationSystem(WorldSystem* world, TaskSystem* tasks,
                                 const AnimationConfig& config,
                                 std::unique_ptr<AnimationRetargeter> retargeter) noexcept
    : world_(world), tasks_(tasks), config_(config), retargeter_(std::move(retargeter))
{
}
AnimationSystem::~AnimationSystem() noexcept
{
    shutdown();
}
bool AnimationSystem::on_initialize(const EngineConfig&)
{
    if (config_.min_parallel_count == 0 || config_.batch_size == 0 ||
        (tasks_ && !tasks_->is_initialized()) || (world_ && !world_->native_world()))
    {
        return false;
    }
    if (world_ && flecs::world(world_->native_world()).has<animation_detail::AnimationServices>())
    {
        return false;
    }
    state_ = std::make_unique<State>();
    if (world_)
    {
        flecs::world(world_->native_world())
            .set<animation_detail::AnimationServices>({this, retargeter_.get()});
    }
    return true;
}
void AnimationSystem::set_event_sink(AnimationEventSink sink)
{
    event_sink_ = std::move(sink);
}
void AnimationSystem::clear() noexcept
{
    if (state_ && world_)
    {
        for (const auto& [id, entry] : state_->entries)
        {
            auto node = world_->find_node(id);
            auto renderer = node ? node.get_component<MeshRenderer>() : MeshRenderer{};
            auto animator = node ? node.get_component<Animator>() : Animator{};
            if (renderer)
            {
                renderer.clear_deformation();
            }
            if (animator && animator.instance_id() == entry->animator_instance)
            {
                engine::ecs_detail::component_runtime(animator)->prepared_revision_ = 0;
                engine::ecs_detail::component_runtime(animator)->committed_models_.clear();
                engine::ecs_detail::component_runtime(animator)->pose_revision_ = 0;
            }
        }
        if (world_->native_world())
        {
            auto query = flecs::world(world_->native_world())
                             .query_builder<BoneAttachment::Runtime>()
                             .build();
            query.each(
                [&](flecs::entity entity, auto&)
                {
                    animation_detail::clear_attachment_drive(
                        world_->find_node(entity.id()).get_component<BoneAttachment>());
                });
        }
    }
    if (state_)
    {
        state_->samples.clear();
        state_->entries.clear();
        state_->clips.clear();
        state_->sampling_clips.clear();
        state_->skeletons.clear();
    }
}
bool AnimationSystem::on_tick(double delta_time)
{
    ENGINE_PROFILE_ZONE("Animation.tick");
    if (!state_ || !std::isfinite(delta_time) || delta_time < 0)
    {
        return false;
    }
    if (!world_ || world_->scenes().empty())
    {
        clear();
        return true;
    }
    if (!world_->refresh_scene_state())
    {
        return false;
    }
    resolve_pending_attachments(*world_);
    auto& samples = state_->samples;
    samples.clear();
    std::set<std::uint64_t> retained;
    std::size_t event_count = 0;
    bool success = true;
    {
        auto query = flecs::world(world_->native_world())
                         .query_builder<const ecs_detail::ComponentStorage<Animator::Data>>()
                         .with<ecs_detail::Published>()
                         .build();
        query.each(
            [&](flecs::entity entity, const auto& stored)
            {
                if (!success)
                {
                    return;
                }
                auto node = world_->find_node(entity.id());
                auto animator = node.get_component<Animator>();
                auto renderer = node.get_component<MeshRenderer>();
                if (!animator || !renderer || !animator.active() || !renderer.active() ||
                    !engine::ecs_detail::component_runtime(animator)->animation_ ||
                    !active_transform_chain(node))
                {
                    if (renderer && state_->entries.contains(node.id()))
                    {
                        renderer.clear_deformation();
                    }
                    if (animator)
                    {
                        engine::ecs_detail::component_runtime(animator)->prepared_revision_ = 0;
                        engine::ecs_detail::component_runtime(animator)->pose_revision_ = 0;
                        engine::ecs_detail::component_runtime(animator)->committed_models_.clear();
                    }
                    return;
                }
                retained.insert(node.id());
                auto* runtime = engine::ecs_detail::component_runtime(animator);
                const auto* mesh = renderer.mesh_data();
                const auto* clip = runtime->animation_.data<AnimationResource>();
                auto found = state_->entries.find(node.id());
                if (!mesh || mesh->skeleton_guid.empty() || mesh->skin.empty() || !clip)
                {
                    success = false;
                    return;
                }
                if (found == state_->entries.end() ||
                    runtime->prepared_revision_ != runtime->revision_ ||
                    found->second->animator_instance != animator.instance_id() ||
                    found->second->mesh_revision != renderer.mesh_revision() ||
                    found->second->mesh_instance != renderer.instance_id())
                {
                    auto candidate = std::make_unique<AnimationEntry>();
                    std::shared_ptr<const animation_detail::CompiledAnimation> compiled;
                    if (!state_->compile(world_->resource_loader(), renderer.mesh_resource(),
                                         runtime->animation_, *mesh, runtime->retarget_profile_,
                                         runtime->retarget_binding_, runtime->source_skeleton_,
                                         compiled) ||
                        !candidate->prepare(*mesh, renderer, animator, compiled))
                    {
                        success = false;
                        return;
                    }
                    if (runtime->retarget_binding_ && runtime->target_skeleton_ &&
                        animation_detail::identity(*runtime->target_skeleton_) !=
                            animation_detail::identity(*compiled->target->resource))
                    {
                        success = false; // profile binding must be re-prepared by an explicit
                                         // atomic play request.
                        return;
                    }
                    if (runtime->fade_seconds_ > 0 && runtime->previous_animation_)
                    {
                        const auto* previous =
                            found != state_->entries.end() ? found->second.get() : nullptr;
                        const bool same_mesh =
                            previous && previous->sampled &&
                            previous->animator_instance == animator.instance_id() &&
                            previous->mesh_instance == renderer.instance_id() &&
                            previous->mesh_revision == renderer.mesh_revision();
                        if (runtime->frozen_pose_ &&
                            animation_detail::identity(*runtime->frozen_pose_->target) ==
                                animation_detail::identity(*compiled->target->resource))
                        {
                            candidate->frozen = runtime->frozen_pose_;
                        }
                        else if (same_mesh &&
                                 (previous->blended ||
                                  animation_detail::identity(*previous->current.clip->resource) !=
                                      animation_detail::identity(*runtime->previous_animation_)))
                        {
                            auto pose = std::make_shared<animation_detail::AnimationPose>();
                            if (!pose->target.retain(previous->current.clip->target->resource))
                            {
                                success = false;
                                return;
                            }
                            pose->locals = previous->committed_locals;
                            candidate->frozen = std::move(pose);
                        }
                        else
                        {
                            std::shared_ptr<const animation_detail::CompiledAnimation> source;
                            if (!state_->compile(world_->resource_loader(),
                                                 renderer.mesh_resource(),
                                                 runtime->previous_animation_, *mesh,
                                                 runtime->previous_retarget_profile_,
                                                 runtime->previous_retarget_binding_,
                                                 runtime->previous_source_skeleton_, source))
                            {
                                success = false;
                                return;
                            }
                            candidate->previous.prepare(std::move(source));
                        }
                    }
                    if (runtime->target_skeleton_ &&
                        animation_detail::identity(*runtime->target_skeleton_) !=
                            animation_detail::identity(*compiled->target->resource))
                    {
                        runtime->ik_targets_.clear();
                    }
                    ResourceReference target, source;
                    if (!target.retain(compiled->target->resource) ||
                        !source.retain(compiled->source))
                    {
                        success = false;
                        return;
                    }
                    runtime->target_skeleton_ = std::move(target);
                    runtime->source_skeleton_ = std::move(source);
                    runtime->frozen_pose_ = candidate->frozen;
                    runtime->prepared_revision_ = runtime->revision_;
                    runtime->pose_revision_ = 0;
                    runtime->committed_models_.clear();
                    runtime->pose_dirty_ = true;
                    state_->entries[node.id()] = std::move(candidate);
                    found = state_->entries.find(node.id());
                }
                auto& entry = *found->second;
                if (runtime->awake_pending_ && !world_->paused())
                {
                    runtime->awake_pending_ = false;
                    if (stored.data.play_on_awake && !animator.play())
                    {
                        success = false;
                        return;
                    }
                }
                const bool running =
                    runtime->playback_ == AnimationPlayback::playing && !world_->paused();
                if (!running && !runtime->pose_dirty_ &&
                    entry.deformation_revision == renderer.deformation_revision())
                {
                    return;
                }
                AnimationSample sample;
                sample.entry = &entry;
                sample.animator = animator;
                sample.renderer = renderer;
                if (!stage_time(sample, running ? delta_time : 0, stored.data, event_count,
                                !world_->paused()))
                {
                    success = false;
                    return;
                }
                sample.frozen_final = !running && !runtime->pose_dirty_ && entry.sampled;
                std::array<float, 16> mesh_world;
                if (!sample.rest && !sample.frozen_final &&
                    (!node.get_component<Transform>().world_matrix(mesh_world.data()) ||
                     !animation_detail::collect_ik_operations(
                         animator, *entry.current.clip->target,
                         animation_detail::load_matrix(mesh_world) * entry.skeleton_to_mesh,
                         sample.ik)))
                {
                    success = false;
                    return;
                }
                samples.push_back(std::move(sample));
            });
    }
    if (!success)
    {
        std::fputs("Animation preparation/time/IK dependency failed\n", stderr);
        return false;
    }
    std::future<bool> sampling;
    ENGINE_PROFILE_PLOT("Animation.samples", static_cast<double>(samples.size()));
    if (tasks_ && samples.size() >= config_.min_parallel_count &&
        samples.size() > config_.batch_size)
    {
        std::vector<TaskDescription> batch;
        for (std::size_t begin = 0; begin < samples.size();)
        {
            const auto end = begin + std::min(config_.batch_size, samples.size() - begin);
            TaskDescription task;
            task.name = "Animation.sample_batch";
            task.execute = [&samples, begin, end] { return sample_range(samples, begin, end); };
            batch.push_back(std::move(task));
            begin = end;
        }
        sampling = tasks_->submit_batch(std::move(batch));
    }
    if (!(sampling.valid() ? sampling.get() : sample_range(samples, 0, samples.size())))
    {
        std::fputs("Animation sampling failed; playback time and events were not committed\n",
                   stderr);
        return false;
    }
    std::vector<AnimationEvent> events;
    for (const auto& sample : samples)
    {
        auto animator = sample.animator;
        auto renderer = sample.renderer;
        if (!animator || !renderer || animator.instance_id() != sample.entry->animator_instance ||
            renderer.mesh_revision() != sample.entry->mesh_revision ||
            renderer.instance_id() != sample.entry->mesh_instance ||
            (!sample.rest &&
             !renderer.set_skinning_palette(sample.entry->palette, sample.entry->normal_palette)))
        {
            return false;
        }
        if (sample.rest)
        {
            renderer.clear_deformation();
        }
        auto* runtime = engine::ecs_detail::component_runtime(animator);
        if (runtime->pose_sequence_ == std::numeric_limits<std::uint64_t>::max())
        {
            return false;
        }
        runtime->time_ = sample.seconds;
        runtime->previous_time_ = sample.previous_seconds;
        runtime->fade_elapsed_ = sample.fade_elapsed;
        runtime->cycle_ = sample.cycle;
        runtime->playback_ = sample.playback;
        runtime->completed_ = sample.completed;
        runtime->event_starting_ = sample.event_starting;
        runtime->pose_revision_ = ++runtime->pose_sequence_;
        runtime->pose_mesh_revision_ = renderer.mesh_revision();
        runtime->pose_mesh_instance_ = renderer.instance_id();
        runtime->committed_models_.resize(sample.entry->models.size());
        for (std::size_t dfs = 0; dfs < sample.entry->models.size(); ++dfs)
        {
            animation_detail::store_matrix(
                sample.entry->models[dfs],
                runtime->committed_models_[sample.entry->current.clip->target->order[dfs]]);
        }
        runtime->pose_dirty_ = false;
        sample.entry->deformation_revision = renderer.deformation_revision();
        sample.entry->committed_locals = sample.entry->locals;
        sample.entry->sampled = true;
        sample.entry->blended = sample.blend < 1;
        if (sample.blend >= 1)
        {
            runtime->previous_animation_.reset();
            runtime->previous_source_skeleton_.reset();
            runtime->previous_retarget_profile_.reset();
            runtime->previous_retarget_binding_.reset();
            runtime->frozen_pose_.reset();
            runtime->fade_seconds_ = runtime->fade_elapsed_ = 0;
            sample.entry->previous = {};
            sample.entry->frozen.reset();
        }
        events.insert(events.end(), sample.events.begin(), sample.events.end());
    }
    for (auto it = state_->entries.begin(); it != state_->entries.end();)
    {
        if (retained.contains(it->first))
        {
            ++it;
        }
        else
        {
            auto node = world_->find_node(it->first);
            auto renderer = node ? node.get_component<MeshRenderer>() : MeshRenderer{};
            if (renderer && renderer.instance_id() == it->second->mesh_instance)
            {
                renderer.clear_deformation();
            }
            it = state_->entries.erase(it);
        }
    }
    std::erase_if(state_->clips, [](const auto& item) { return item.second.use_count() == 1; });
    std::erase_if(state_->sampling_clips,
                  [](const auto& item) { return item.second.use_count() == 1; });
    std::erase_if(state_->skeletons, [](const auto& item) { return item.second.use_count() == 1; });
    if (!publish_attachments(*world_))
    {
        return false;
    }
    // 已离开ECS遍历并等待全部worker；sink可安全使用普通场景操作。
    auto sink = event_sink_;
    return events.empty() || !sink || sink(events);
}
void AnimationSystem::on_shutdown() noexcept
{
    clear();
    if (world_ && world_->native_world())
    {
        auto world = flecs::world(world_->native_world());
        const auto* services = world.try_get<animation_detail::AnimationServices>();
        if (services && services->owner == this)
        {
            world.remove<animation_detail::AnimationServices>();
        }
    }
    state_.reset();
    event_sink_ = {};
}
} // namespace engine
