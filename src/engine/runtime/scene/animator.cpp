#include "runtime/scene/animator.hpp"

#include "runtime/animation/animation_binding.hpp"
#include "runtime/core/guid_map.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/node.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace engine
{

Animator::Animator(Node& node) : Component(node)
{
    mutable_data().payload.Set(schema::AnimatorDataT{});
}

bool Animator::valid_settings(const schema::AnimatorDataT& settings) noexcept
{
    return (settings.animation_guid.empty() || GuidMap::valid_guid(settings.animation_guid)) &&
           std::isfinite(settings.speed) && settings.speed >= 0.0F && settings.speed <= 16.0F;
}

const schema::AnimatorDataT& Animator::settings() const noexcept
{
    return *data().payload.AsAnimatorData();
}

bool Animator::set_settings(const schema::AnimatorDataT& settings)
{
    if (!valid_settings(settings))
    {
        return false;
    }
    const bool changed = settings.animation_guid != this->settings().animation_guid;
    auto replacement = animation_;
    if (changed)
    {
        if (revision_ == std::numeric_limits<std::uint64_t>::max())
        {
            return false;
        }
        replacement.reset();
        if (!settings.animation_guid.empty())
        {
            auto* resources = ResourceSystem::instance();
            if (!resources || !resources->loader().load(settings.animation_guid, replacement) ||
                !replacement || !replacement->animation())
            {
                return false;
            }
        }
    }
    *mutable_data().payload.AsAnimatorData() = settings;
    animation_ = std::move(replacement);
    if (changed)
    {
        ++revision_;
        target_skeleton_.reset();
        prepared_revision_ = 0;
        const bool stopped = stop();
        (void)stopped;
        awake_pending_ = settings.play_on_awake;
    }
    else if (!settings.play_on_awake)
    {
        awake_pending_ = false;
    }
    return true;
}

bool Animator::restore_data(const schema::ComponentDataT& data)
{
    const auto* settings = data.payload.AsAnimatorData();
    if (!settings || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(data.enabled);
    const bool stopped = stop();
    (void)stopped;
    awake_pending_ = settings->play_on_awake;
    prepared_revision_ = 0;
    return true;
}

bool Animator::set_animation(std::string_view path)
{
    auto replacement = settings();
    if (path.empty())
    {
        replacement.animation_guid.clear();
    }
    else if (!ResourceSystem::path_to_guid(path, replacement.animation_guid))
    {
        return false;
    }
    if (!set_settings(replacement))
    {
        return false;
    }
    // 显式直接选择同一动作也结束过渡，但保留原有播放时间。
    previous_animation_.reset();
    frozen_pose_.reset();
    fade_seconds_ = 0.0;
    fade_elapsed_ = 0.0;
    pose_dirty_ = playback_ != AnimationPlayback::stopped;
    return true;
}

bool Animator::cross_fade(std::string_view path, double seconds)
{
    auto* renderer = node().get_component<MeshRenderer>();
    const auto* mesh = renderer != nullptr ? renderer->mesh_data() : nullptr;
    auto* resources = ResourceSystem::instance();
    Guid guid;
    ResourcePtr candidate;
    ResourcePtr target;
    ResourcePtr source;
    if (!enabled() || path.empty() || !std::isfinite(seconds) || seconds < 0.0 ||
        revision_ == std::numeric_limits<std::uint64_t>::max() || mesh == nullptr ||
        mesh->skeleton_guid.empty() || resources == nullptr ||
        !ResourceSystem::path_to_guid(path, guid) || !resources->loader().load(guid, candidate) ||
        candidate == nullptr || candidate->animation() == nullptr ||
        !resources->loader().load(mesh->skeleton_guid, target) || target == nullptr ||
        target->skeleton() == nullptr ||
        !resources->loader().load(candidate->animation()->skeleton_guid, source) ||
        source == nullptr || source->skeleton() == nullptr ||
        !animation_detail::valid_binding(*mesh, *target->skeleton(), *source->skeleton(),
                                         *candidate->animation()))
    {
        return false;
    }
    if (animation_ != nullptr && animation_->info().guid == candidate->info().guid &&
        animation_->generation() == candidate->generation() &&
        playback_ != AnimationPlayback::stopped)
    {
        return true;
    }
    if (seconds > 0.0 && animation_ != nullptr && playback_ != AnimationPlayback::stopped)
    {
        ResourcePtr previous_source;
        if (!resources->loader().load(animation_->animation()->skeleton_guid, previous_source) ||
            previous_source == nullptr || previous_source->skeleton() == nullptr ||
            !animation_detail::valid_binding(
                *mesh, *target->skeleton(), *previous_source->skeleton(), *animation_->animation()))
        {
            return false;
        }
    }
    frozen_pose_.reset();
    previous_animation_ = animation_;
    previous_time_ = time_;
    previous_speed_ = settings().speed;
    previous_loop_ = settings().loop;
    previous_advancing_ = playback_ == AnimationPlayback::playing;
    if (playback_ == AnimationPlayback::stopped)
    {
        previous_animation_.reset();
    }
    mutable_data().payload.AsAnimatorData()->animation_guid = std::move(guid);
    animation_ = std::move(candidate);
    target_skeleton_ = std::move(target);
    ++revision_;
    time_ = 0.0;
    fade_seconds_ = seconds;
    fade_elapsed_ = 0.0;
    playback_ = AnimationPlayback::playing;
    awake_pending_ = false;
    pose_dirty_ = true;
    return true;
}

std::string_view Animator::animation_path() const noexcept
{
    return animation_ ? std::string_view(animation_->info().path) : std::string_view{};
}

bool Animator::play() noexcept
{
    if (!enabled() || !animation_ || !animation_->animation())
    {
        return false;
    }
    if (playback_ == AnimationPlayback::stopped)
    {
        time_ = 0.0;
    }
    playback_ = AnimationPlayback::playing;
    awake_pending_ = false;
    pose_dirty_ = true;
    return true;
}

bool Animator::pause() noexcept
{
    if (playback_ == AnimationPlayback::stopped)
    {
        return false;
    }
    playback_ = AnimationPlayback::paused;
    awake_pending_ = false;
    return true;
}

bool Animator::stop() noexcept
{
    previous_animation_.reset();
    frozen_pose_.reset();
    fade_seconds_ = 0.0;
    fade_elapsed_ = 0.0;
    playback_ = AnimationPlayback::stopped;
    time_ = 0.0;
    awake_pending_ = false;
    pose_dirty_ = false;
    auto* renderer = node().get_component<MeshRenderer>();
    if (renderer)
    {
        renderer->clear_deformation();
    }
    return true;
}

bool Animator::seek(double seconds) noexcept
{
    const auto* animation = animation_ ? animation_->animation() : nullptr;
    if (!animation || !std::isfinite(seconds) || seconds < 0.0 || seconds > animation->duration)
    {
        return false;
    }
    previous_animation_.reset();
    frozen_pose_.reset();
    fade_seconds_ = 0.0;
    fade_elapsed_ = 0.0;
    time_ = seconds;
    if (playback_ == AnimationPlayback::stopped)
    {
        playback_ = AnimationPlayback::paused;
    }
    pose_dirty_ = true;
    awake_pending_ = false;
    return true;
}

AnimationPlayback Animator::playback() const noexcept
{
    return playback_;
}

double Animator::time() const noexcept
{
    return time_;
}

} // namespace engine
