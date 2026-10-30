#include "runtime/scene/animator.hpp"

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
    return set_settings(replacement);
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
