#pragma once

#include "runtime/scene/animator.hpp"
#include "runtime/scene/component_runtime.hpp"

namespace engine
{
struct AnimatorRuntime
{
    std::shared_ptr<const Resource> animation_;
    std::shared_ptr<const Resource> target_skeleton_;
    std::shared_ptr<const Resource> source_skeleton_;
    std::shared_ptr<const Resource> previous_source_skeleton_;
    std::shared_ptr<const Resource> previous_animation_;
    std::shared_ptr<const animation_detail::AnimationPose> frozen_pose_;
    double previous_time_ = 0.0;
    float previous_speed_ = 1.0F;
    bool previous_loop_ = true;
    bool previous_advancing_ = false;
    double fade_seconds_ = 0.0;
    double fade_elapsed_ = 0.0;
    std::uint64_t revision_ = 1;
    std::uint64_t prepared_revision_ = 0;
    double time_ = 0.0;
    AnimationPlayback playback_ = AnimationPlayback::stopped;
    std::shared_ptr<const Resource> retarget_profile_;
    std::shared_ptr<const Resource> previous_retarget_profile_;
    std::shared_ptr<const AnimationRetargetBinding> retarget_binding_;
    std::shared_ptr<const AnimationRetargetBinding> previous_retarget_binding_;
    std::vector<std::array<float, 16>> committed_models_;
    std::map<std::string, AnimationIkTarget, std::less<>> ik_targets_;
    std::vector<schema::AnimationEventMarkerDataT> event_markers_;
    std::uint64_t playback_id_ = 0;
    std::uint64_t playback_sequence_ = 0;
    std::uint64_t cycle_ = 0;
    std::uint64_t pose_revision_ = 0;
    std::uint64_t pose_sequence_ = 0;
    std::uint64_t pose_mesh_revision_ = 0;
    std::uint64_t pose_mesh_instance_ = 0;
    std::uint64_t track_revision_ = 1;
    bool completed_ = false;
    bool event_starting_ = false;
    bool awake_pending_ = true;
    bool pose_dirty_ = false;
};
} // namespace engine
