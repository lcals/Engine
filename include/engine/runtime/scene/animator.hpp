#pragma once

#include "runtime/animation/animation_events.hpp"
#include "runtime/animation/animation_types.hpp"
#include "runtime/scene/component.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine
{

class AnimationSystem;
class Resource;
class AnimationRetargetBinding;
namespace animation_detail
{
struct AnimationPose;
}

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

class Animator final : public Component
{
public:
    using Data = schema::AnimatorDataT;
    Animator() noexcept = default;
    explicit Animator(Component component) noexcept : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    using Runtime = AnimatorRuntime;
    [[nodiscard]] Runtime* runtime() const noexcept;
    static constexpr ComponentType component_type = ComponentType::AnimatorData;
    [[nodiscard]] static bool valid_settings(const schema::AnimatorDataT& settings) noexcept;
    [[nodiscard]] schema::AnimatorDataT settings() const noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const schema::AnimatorDataT& settings);
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
    [[nodiscard]] bool set_animation(std::string_view path);
    [[nodiscard]] bool play_animation(const AnimationPlayRequest& request);
    [[nodiscard]] bool get_playback_info(AnimatorPlaybackInfo& output) const noexcept;
    [[nodiscard]] bool get_bone_pose(std::string_view path, AnimationPoseTransform& output) const;
    [[nodiscard]] bool get_socket_pose(std::string_view name, AnimationPoseTransform& output) const;
    [[nodiscard]] bool set_aim_ik_target(std::string_view name, const AnimationIkTarget& target);
    [[nodiscard]] bool set_two_bone_ik_target(std::string_view name,
                                              const AnimationIkTarget& target);
    [[nodiscard]] bool get_ik_target(std::string_view name, AnimationIkTarget& output) const;
    [[nodiscard]] bool
    set_event_markers(std::span<const schema::AnimationEventMarkerDataT> markers);
    void clear_event_markers() noexcept;
    // 校验后从当前姿态过渡；非负秒数，零秒直接切换，失败保留原状态。
    [[nodiscard]] bool cross_fade(std::string_view path, double seconds);
    [[nodiscard]] std::string_view animation_path() const noexcept;
    [[nodiscard]] bool play() noexcept;
    [[nodiscard]] bool pause() noexcept;
    [[nodiscard]] bool stop() noexcept;
    [[nodiscard]] bool seek(double seconds) noexcept;
    [[nodiscard]] AnimationPlayback playback() const noexcept;
    [[nodiscard]] double time() const noexcept;
};

} // namespace engine
