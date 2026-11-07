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

namespace engine::resource_detail
{
class ResourceReference;
}

namespace engine
{

class AnimationSystem;
class Resource;
class AnimationRetargetBinding;
namespace animation_detail
{
struct AnimationPose;
}

struct AnimatorRuntime;

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

private:
    [[nodiscard]] bool apply_animation(const AnimationPlayRequest& request,
                                       resource_detail::ResourceReference clip);
    [[nodiscard]] bool apply_settings(const Data& settings,
                                      resource_detail::ResourceReference prepared);
    template <class T> friend auto engine::ecs_detail::component_runtime(T component) noexcept;
    [[nodiscard]] Runtime* runtime() const noexcept;
};

} // namespace engine
