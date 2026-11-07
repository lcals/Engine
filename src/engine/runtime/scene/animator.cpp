#include "runtime/scene/animator.hpp"

#include "runtime/scene/animator_runtime.hpp"

#include "runtime/animation/animation_binding.hpp"
#include "runtime/animation/animation_ik.hpp"
#include "runtime/animation/animation_math.hpp"
#include "runtime/animation/animation_retargeter.hpp"
#include "runtime/animation/animation_services.hpp"
#include "runtime/core/guid_map.hpp"
#include "runtime/resource/codecs/animation_resource_codec.hpp"
#include "runtime/resource/types/animation_resource.hpp"
#include "runtime/resource/types/animation_retarget_profile_resource.hpp"
#include "runtime/resource/types/skeleton_resource.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/resource_binding.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace engine
{
namespace
{
bool same_resource(const resource_detail::ResourceReference& a,
                   const resource_detail::ResourceReference& b)
{
    return (!a && !b) ||
           (a && b && a->info().guid == b->info().guid && a->generation() == b->generation());
}
bool validate_overlay(const schema::AnimationDataT& animation,
                      std::span<const schema::AnimationEventMarkerDataT> markers)
{
    auto data = animation;
    for (const auto& marker : markers)
    {
        data.events.push_back(std::make_unique<schema::AnimationEventMarkerDataT>(marker));
    }
    return AnimationResourceCodec::validate(data);
}
bool target_skeleton(Animator animator, resource_detail::ResourceReference& target)
{
    target.reset();
    const auto renderer = animator.node().get_component<MeshRenderer>();
    const auto* mesh = renderer ? renderer.mesh_data() : nullptr;
    auto* resources = animator.node().world()->resource_loader();
    return mesh && resources && !mesh->skeleton_guid.empty() &&
           target.dependency(resources, renderer.mesh_resource(), mesh->skeleton_guid) && target &&
           target.data<SkeletonResource>();
}
bool committed_pose(Animator animator, resource_detail::ResourceReference& target,
                    MeshRenderer& renderer)
{
    target.reset();
    renderer = {};
    if (!animator || !animator.active() ||
        !engine::ecs_detail::component_runtime(animator)->pose_revision_ ||
        !flecs::world(animator.node().world()->native_world())
             .entity(animator.node().id())
             .has<ecs_detail::Published>())
    {
        return false;
    }
    renderer = animator.node().get_component<MeshRenderer>();
    if (!target.retain(engine::ecs_detail::component_runtime(animator)->target_skeleton_))
    {
        return false;
    }
    const auto* mesh = renderer ? renderer.mesh_data() : nullptr;
    return renderer && renderer.active() && mesh && target && target.data<SkeletonResource>() &&
           mesh->skeleton_guid == target->info().guid &&
           engine::ecs_detail::component_runtime(animator)->pose_mesh_revision_ ==
               renderer.mesh_revision() &&
           engine::ecs_detail::component_runtime(animator)->pose_mesh_instance_ ==
               renderer.instance_id() &&
           engine::ecs_detail::component_runtime(animator)->committed_models_.size() ==
               target.data<SkeletonResource>()->joints.size();
}
} // namespace

bool Animator::register_type(ComponentRegistry& registry)
{
    return register_component_storage<Animator, Data, Runtime>(registry);
}
Animator::Runtime* Animator::runtime() const noexcept
{
    return runtime_state<Runtime>();
}
bool Animator::valid_settings(const Data& data) noexcept
{
    return (data.animation_guid.empty() || GuidMap::valid_guid(data.animation_guid)) &&
           std::isfinite(data.speed) && data.speed >= 0.0F && data.speed <= 16.0F;
}
Animator::Data Animator::settings() const noexcept
{
    Data output{};
    (void)get_settings(output);
    return output;
}
bool Animator::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}
bool Animator::set_settings(const Data& data)
{
    return apply_settings(data, {});
}

bool Animator::apply_settings(const Data& data, resource_detail::ResourceReference prepared)
{
    if (!writable() || !valid_settings(data))
    {
        return false;
    }
    auto* state = runtime();
    const bool changed = data.animation_guid != settings().animation_guid;
    resource_detail::ResourceReference replacement;
    if (state->animation_ && !replacement.retain(state->animation_))
    {
        return false;
    }
    if (changed)
    {
        if (state->revision_ == std::numeric_limits<std::uint64_t>::max())
        {
            return false;
        }
        replacement = std::move(prepared);
        if (!data.animation_guid.empty())
        {
            auto* resources = node().world()->resource_loader();
            if (!resources ||
                (!replacement &&
                 !ecs_detail::acquire_resource(node(), data.animation_guid, replacement)) ||
                !replacement || !replacement.data<AnimationResource>() ||
                !validate_overlay(*replacement.data<AnimationResource>(), state->event_markers_))
            {
                return false;
            }
        }
    }
    *write_data<Data>() = data;
    modified();
    state->animation_ = std::move(replacement);
    if (changed)
    {
        ++state->revision_;
        state->target_skeleton_.reset();
        state->source_skeleton_.reset();
        state->retarget_profile_.reset();
        state->retarget_binding_.reset();
        state->prepared_revision_ = 0;
        (void)stop();
        state->awake_pending_ = data.play_on_awake;
    }
    else if (!data.play_on_awake)
    {
        state->awake_pending_ = false;
    }
    return true;
}
bool Animator::restore_data(const schema::ComponentDataT& input)
{
    const auto* data = input.payload.AsAnimatorData();
    if (!writable() || !input.flags || !data || !set_settings(*data) || !restore_flags(input))
    {
        return false;
    }
    (void)stop();
    runtime()->awake_pending_ = data->play_on_awake;
    runtime()->prepared_revision_ = 0;
    return true;
}
bool Animator::set_animation(std::string_view path)
{
    if (!writable())
    {
        return false;
    }
    resource_detail::ResourceReference prepared;
    auto data = settings();
    if (path.empty())
    {
        data.animation_guid.clear();
    }
    else if (!ecs_detail::acquire_resource(node(), path, prepared))
    {
        return false;
    }
    if (prepared)
    {
        data.animation_guid = prepared->info().guid;
    }
    if (!apply_settings(data, std::move(prepared)))
    {
        return false;
    }
    auto* state = runtime();
    if (state->retarget_binding_)
    {
        if (state->revision_ == std::numeric_limits<std::uint64_t>::max())
        {
            return false;
        }
        ++state->revision_;
    }
    state->retarget_binding_.reset();
    state->retarget_profile_.reset();
    state->previous_animation_.reset();
    state->previous_source_skeleton_.reset();
    state->previous_retarget_binding_.reset();
    state->previous_retarget_profile_.reset();
    state->frozen_pose_.reset();
    state->fade_seconds_ = state->fade_elapsed_ = 0.0;
    state->pose_dirty_ = true;
    return true;
}
bool Animator::play_animation(const AnimationPlayRequest& request)
{
    return apply_animation(request, {});
}
bool Animator::apply_animation(const AnimationPlayRequest& request,
                               resource_detail::ResourceReference clip)
{
    if (!writable() || !active() || request.animation_path.empty() ||
        !std::isfinite(request.fade_seconds) || request.fade_seconds < 0 ||
        !std::isfinite(request.speed) || request.speed < 0 || request.speed > 16)
    {
        return false;
    }
    auto* state = runtime();
    const auto renderer = node().get_component<MeshRenderer>();
    const auto* mesh = renderer ? renderer.mesh_data() : nullptr;
    auto* resources = node().world()->resource_loader();
    resource_detail::ResourceReference source, target, profile;
    if (!resources || !mesh || mesh->skeleton_guid.empty() ||
        (!clip && !ecs_detail::acquire_resource(node(), request.animation_path, clip)) || !clip ||
        !clip.data<AnimationResource>() ||
        !source.dependency(resources, clip.handle(),
                           clip.data<AnimationResource>()->skeleton_guid) ||
        !source || !source.data<SkeletonResource>() || !target_skeleton(*this, target) ||
        !validate_overlay(*clip.data<AnimationResource>(), state->event_markers_))
    {
        return false;
    }
    std::shared_ptr<const AnimationRetargetBinding> binding;
    if (request.retarget_profile_path.empty())
    {
        if (!animation_detail::valid_binding(*mesh, *target.data<SkeletonResource>(),
                                             *source.data<SkeletonResource>(),
                                             *clip.data<AnimationResource>()))
        {
            return false;
        }
    }
    else
    {
        const auto* services = flecs::world(node().world()->native_world())
                                   .try_get<animation_detail::AnimationServices>();
        std::string error;
        schema::AnimationDataT rest;
        if (!services || !services->retargeter ||
            !ecs_detail::acquire_resource(node(), request.retarget_profile_path, profile) ||
            !profile || !profile.data<AnimationRetargetResource>() ||
            profile.data<AnimationRetargetResource>()->source_skeleton_guid !=
                source->info().guid ||
            profile.data<AnimationRetargetResource>()->target_skeleton_guid !=
                target->info().guid ||
            !animation_detail::valid_binding(*mesh, *target.data<SkeletonResource>(),
                                             *target.data<SkeletonResource>(), rest) ||
            !services->retargeter->prepare(*profile.data<AnimationRetargetResource>(),
                                           *source.data<SkeletonResource>(),
                                           *target.data<SkeletonResource>(), binding, error) ||
            !binding)
        {
            return false;
        }
    }
    const bool same = same_resource(state->animation_, clip) &&
                      same_resource(state->retarget_profile_, profile) &&
                      same_resource(state->target_skeleton_, target) &&
                      same_resource(state->source_skeleton_, source);
    if (!request.restart && same && state->playback_ != AnimationPlayback::stopped)
    {
        auto* data = write_data<Data>();
        data->speed = request.speed;
        data->loop = request.loop;
        state->playback_ = AnimationPlayback::playing;
        state->completed_ = false;
        state->awake_pending_ = false;
        state->pose_dirty_ = true;
        modified();
        return true;
    }
    if (state->revision_ == std::numeric_limits<std::uint64_t>::max() ||
        state->playback_sequence_ == std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    if (request.fade_seconds > 0 && state->animation_ &&
        state->playback_ != AnimationPlayback::stopped && state->target_skeleton_ &&
        !same_resource(state->target_skeleton_, target))
    {
        return false;
    }
    state->previous_animation_ = std::move(state->animation_);
    state->previous_source_skeleton_ = std::move(state->source_skeleton_);
    state->previous_retarget_profile_ = std::move(state->retarget_profile_);
    state->previous_retarget_binding_ = state->retarget_binding_;
    state->previous_time_ = state->time_;
    state->previous_speed_ = settings().speed;
    state->previous_loop_ = settings().loop;
    state->previous_advancing_ = state->playback_ == AnimationPlayback::playing;
    state->frozen_pose_.reset();
    if (!same_resource(state->target_skeleton_, target))
    {
        state->ik_targets_.clear();
        state->pose_revision_ = 0;
        state->committed_models_.clear();
    }
    auto* data = write_data<Data>();
    data->animation_guid = clip->info().guid;
    data->speed = request.speed;
    data->loop = request.loop;
    state->animation_ = std::move(clip);
    state->target_skeleton_ = std::move(target);
    state->source_skeleton_ = std::move(source);
    state->retarget_profile_ = std::move(profile);
    state->retarget_binding_ = std::move(binding);
    ++state->revision_;
    state->time_ = 0;
    state->cycle_ = 0;
    state->completed_ = false;
    state->playback_id_ = ++state->playback_sequence_;
    state->event_starting_ = true;
    state->fade_seconds_ = request.fade_seconds;
    state->fade_elapsed_ = 0;
    state->playback_ = AnimationPlayback::playing;
    state->awake_pending_ = false;
    state->pose_dirty_ = true;
    modified();
    return true;
}
bool Animator::cross_fade(std::string_view path, double seconds)
{
    if (!writable() || !active() || !std::isfinite(seconds) || seconds < 0 || path.empty())
    {
        return false;
    }
    // 旧接口的同一资源 no-op（包括保持暂停）保留。
    resource_detail::ResourceReference clip;
    if (!ecs_detail::acquire_resource(node(), path, clip) || !clip ||
        !clip.data<AnimationResource>())
    {
        return false;
    }
    if (same_resource(runtime()->animation_, clip) &&
        runtime()->playback_ != AnimationPlayback::stopped)
    {
        return true;
    }
    AnimationPlayRequest request;
    request.animation_path = std::string(path);
    request.fade_seconds = seconds;
    request.speed = settings().speed;
    request.loop = settings().loop;
    return apply_animation(request, std::move(clip));
}
std::string_view Animator::animation_path() const noexcept
{
    return valid() && runtime()->animation_ ? std::string_view(runtime()->animation_->info().path)
                                            : std::string_view{};
}
bool Animator::play() noexcept
{
    if (!writable() || !active() || !runtime()->animation_ ||
        !runtime()->animation_.data<AnimationResource>())
    {
        return false;
    }
    auto* state = runtime();
    if (state->playback_ == AnimationPlayback::stopped)
    {
        if (state->playback_sequence_ == std::numeric_limits<std::uint64_t>::max())
        {
            return false;
        }
        state->time_ = 0;
        state->cycle_ = 0;
        state->playback_id_ = ++state->playback_sequence_;
        state->event_starting_ = true;
    }
    state->playback_ = AnimationPlayback::playing;
    state->completed_ = false;
    state->awake_pending_ = false;
    state->pose_dirty_ = true;
    return true;
}
bool Animator::pause() noexcept
{
    if (!writable() || runtime()->playback_ == AnimationPlayback::stopped)
    {
        return false;
    }
    runtime()->playback_ = AnimationPlayback::paused;
    runtime()->awake_pending_ = false;
    return true;
}
bool Animator::stop() noexcept
{
    if (!writable())
    {
        return false;
    }
    auto* state = runtime();
    state->previous_animation_.reset();
    state->previous_source_skeleton_.reset();
    state->previous_retarget_profile_.reset();
    state->previous_retarget_binding_.reset();
    state->frozen_pose_.reset();
    state->fade_seconds_ = state->fade_elapsed_ = 0;
    state->playback_ = AnimationPlayback::stopped;
    state->time_ = 0;
    state->cycle_ = 0;
    state->playback_id_ = 0;
    state->completed_ = false;
    state->event_starting_ = false;
    state->awake_pending_ = false;
    state->pose_dirty_ = true;
    state->pose_revision_ = 0;
    state->committed_models_.clear();
    auto renderer = node().get_component<MeshRenderer>();
    if (renderer)
    {
        renderer.clear_deformation();
    }
    node().world()->invalidate_scene_state();
    return true;
}
bool Animator::seek(double seconds) noexcept
{
    if (!writable())
    {
        return false;
    }
    auto* state = runtime();
    const auto* animation =
        state->animation_ ? state->animation_.data<AnimationResource>() : nullptr;
    if (!animation || !std::isfinite(seconds) || seconds < 0 || seconds > animation->duration ||
        state->playback_sequence_ == std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    state->previous_animation_.reset();
    state->previous_source_skeleton_.reset();
    state->previous_retarget_profile_.reset();
    state->previous_retarget_binding_.reset();
    state->frozen_pose_.reset();
    state->fade_seconds_ = state->fade_elapsed_ = 0;
    state->time_ = seconds;
    state->cycle_ = 0;
    state->playback_id_ = ++state->playback_sequence_;
    state->completed_ = false;
    state->event_starting_ = false;
    if (state->playback_ == AnimationPlayback::stopped)
    {
        state->playback_ = AnimationPlayback::paused;
    }
    state->pose_dirty_ = true;
    state->awake_pending_ = false;
    return true;
}
AnimationPlayback Animator::playback() const noexcept
{
    return valid() ? runtime()->playback_ : AnimationPlayback::stopped;
}
double Animator::time() const noexcept
{
    return valid() ? runtime()->time_ : 0;
}
bool Animator::get_playback_info(AnimatorPlaybackInfo& output) const noexcept
{
    output = {};
    if (!valid())
    {
        return false;
    }
    const auto* state = runtime();
    const auto* animation =
        state->animation_ ? state->animation_.data<AnimationResource>() : nullptr;
    output.playback = state->playback_;
    output.time = state->time_;
    output.duration = animation ? animation->duration : 0;
    output.normalized_time =
        output.duration > 0 ? static_cast<double>(state->cycle_) + output.time / output.duration
                            : 0;
    output.completed = state->completed_;
    output.transitioning = state->fade_seconds_ > 0 && state->fade_elapsed_ < state->fade_seconds_;
    output.playback_id = state->playback_id_;
    return true;
}
bool Animator::get_bone_pose(std::string_view path, AnimationPoseTransform& output) const
{
    output = {};
    resource_detail::ResourceReference target;
    MeshRenderer renderer;
    std::vector<std::string> paths;
    if (!committed_pose(*this, target, renderer) ||
        !animation_detail::readable_joint_paths(*target.data<SkeletonResource>(), paths))
    {
        return false;
    }
    const auto found = std::find(paths.begin(), paths.end(), path);
    std::array<float, 16> mesh_world;
    ozz::math::Float4x4 inverse;
    if (found == paths.end() || !node().get_component<Transform>().world_matrix(mesh_world.data()))
    {
        return false;
    }
    std::array<float, 16> binding;
    const auto* mesh = renderer.mesh_data();
    std::copy(mesh->mesh_to_skeleton.begin(), mesh->mesh_to_skeleton.end(), binding.begin());
    if (!animation_detail::invert_matrix(animation_detail::load_matrix(binding), inverse))
    {
        return false;
    }
    output.model = runtime()->committed_models_[static_cast<std::size_t>(found - paths.begin())];
    animation_detail::store_matrix(animation_detail::load_matrix(mesh_world) * inverse *
                                       animation_detail::load_matrix(output.model),
                                   output.world);
    output.pose_revision = runtime()->pose_revision_;
    return true;
}
bool Animator::get_socket_pose(std::string_view name, AnimationPoseTransform& output) const
{
    output = {};
    resource_detail::ResourceReference target;
    MeshRenderer renderer;
    if (!committed_pose(*this, target, renderer))
    {
        return false;
    }
    for (const auto& socket : target.data<SkeletonResource>()->sockets)
    {
        if (socket && socket->name == name)
        {
            AnimationPoseTransform bone;
            if (!get_bone_pose(socket->joint_path, bone))
            {
                return false;
            }
            const auto offset = animation_detail::pose_matrix(socket->offset);
            animation_detail::store_matrix(animation_detail::load_matrix(bone.model) * offset,
                                           output.model);
            animation_detail::store_matrix(animation_detail::load_matrix(bone.world) * offset,
                                           output.world);
            output.pose_revision = bone.pose_revision;
            return true;
        }
    }
    return false;
}
bool Animator::set_aim_ik_target(std::string_view name, const AnimationIkTarget& target)
{
    return writable() && animation_detail::set_ik_target(*this, name, target, false);
}
bool Animator::set_two_bone_ik_target(std::string_view name, const AnimationIkTarget& target)
{
    return writable() && animation_detail::set_ik_target(*this, name, target, true);
}
bool Animator::get_ik_target(std::string_view name, AnimationIkTarget& output) const
{
    output = {};
    if (!valid())
    {
        return false;
    }
    const auto found = runtime()->ik_targets_.find(name);
    if (found == runtime()->ik_targets_.end())
    {
        return false;
    }
    output = found->second;
    return true;
}
bool Animator::set_event_markers(std::span<const schema::AnimationEventMarkerDataT> markers)
{
    if (!writable() || !runtime()->animation_ || !runtime()->animation_.data<AnimationResource>() ||
        runtime()->track_revision_ == std::numeric_limits<std::uint64_t>::max() ||
        !validate_overlay(*runtime()->animation_.data<AnimationResource>(), markers))
    {
        return false;
    }
    runtime()->event_markers_.assign(markers.begin(), markers.end());
    ++runtime()->track_revision_;
    return true;
}
void Animator::clear_event_markers() noexcept
{
    if (writable())
    {
        runtime()->event_markers_.clear();
        if (runtime()->track_revision_ != std::numeric_limits<std::uint64_t>::max())
        {
            ++runtime()->track_revision_;
        }
    }
}
} // namespace engine
