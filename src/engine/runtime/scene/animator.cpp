#include "runtime/scene/animator.hpp"

#include "runtime/scene/ecs_storage.hpp"

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

bool Animator::register_type(ComponentRegistry& registry)
{
    return register_component_storage<Animator, Data, Runtime>(registry);
}

Animator::Runtime* Animator::runtime() const noexcept
{
    return runtime_state<Runtime>();
}

bool Animator::valid_settings(const schema::AnimatorDataT& settings) noexcept
{
    return (settings.animation_guid.empty() || GuidMap::valid_guid(settings.animation_guid)) &&
           std::isfinite(settings.speed) && settings.speed >= 0.0F && settings.speed <= 16.0F;
}

schema::AnimatorDataT Animator::settings() const noexcept
{
    const auto* value = read_data<Data>();
    return value ? *value : Data{};
}

bool Animator::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool Animator::set_settings(const schema::AnimatorDataT& settings)
{
    if (!writable())
    {
        return false;
    }
    if (!valid_settings(settings))
    {
        return false;
    }
    const bool changed = settings.animation_guid != this->settings().animation_guid;
    auto replacement = runtime()->animation_;
    if (changed)
    {
        if (runtime()->revision_ == std::numeric_limits<std::uint64_t>::max())
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
    *write_data<Data>() = settings;
    modified();
    runtime()->animation_ = std::move(replacement);
    if (changed)
    {
        ++runtime()->revision_;
        runtime()->target_skeleton_.reset();
        runtime()->prepared_revision_ = 0;
        const bool stopped = stop();
        (void)stopped;
        runtime()->awake_pending_ = settings.play_on_awake;
    }
    else if (!settings.play_on_awake)
    {
        runtime()->awake_pending_ = false;
    }
    return true;
}

bool Animator::restore_data(const schema::ComponentDataT& data)
{
    if (!data.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* settings = data.payload.AsAnimatorData();
    if (!settings || !set_settings(*settings))
    {
        return false;
    }
    if (!restore_flags(data))
    {
        return false;
    }
    const bool stopped = stop();
    (void)stopped;
    runtime()->awake_pending_ = settings->play_on_awake;
    runtime()->prepared_revision_ = 0;
    return true;
}

bool Animator::set_animation(std::string_view path)
{
    if (!writable())
    {
        return false;
    }
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
    runtime()->previous_animation_.reset();
    runtime()->frozen_pose_.reset();
    runtime()->fade_seconds_ = 0.0;
    runtime()->fade_elapsed_ = 0.0;
    runtime()->pose_dirty_ = runtime()->playback_ != AnimationPlayback::stopped;
    return true;
}

bool Animator::cross_fade(std::string_view path, double seconds)
{
    if (!writable())
    {
        return false;
    }
    auto renderer = node().get_component<MeshRenderer>();
    const auto* mesh = renderer.valid() ? renderer.mesh_data() : nullptr;
    auto* resources = ResourceSystem::instance();
    Guid guid;
    ResourcePtr candidate;
    ResourcePtr target;
    ResourcePtr source;
    if (!active() || path.empty() || !std::isfinite(seconds) || seconds < 0.0 ||
        runtime()->revision_ == std::numeric_limits<std::uint64_t>::max() || mesh == nullptr ||
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
    if (runtime()->animation_ != nullptr &&
        runtime()->animation_->info().guid == candidate->info().guid &&
        runtime()->animation_->generation() == candidate->generation() &&
        runtime()->playback_ != AnimationPlayback::stopped)
    {
        return true;
    }
    if (seconds > 0.0 && runtime()->animation_ != nullptr &&
        runtime()->playback_ != AnimationPlayback::stopped)
    {
        ResourcePtr previous_source;
        if (!resources->loader().load(runtime()->animation_->animation()->skeleton_guid,
                                      previous_source) ||
            previous_source == nullptr || previous_source->skeleton() == nullptr ||
            !animation_detail::valid_binding(*mesh, *target->skeleton(),
                                             *previous_source->skeleton(),
                                             *runtime()->animation_->animation()))
        {
            return false;
        }
    }
    runtime()->frozen_pose_.reset();
    runtime()->previous_animation_ = runtime()->animation_;
    runtime()->previous_time_ = runtime()->time_;
    runtime()->previous_speed_ = settings().speed;
    runtime()->previous_loop_ = settings().loop;
    runtime()->previous_advancing_ = runtime()->playback_ == AnimationPlayback::playing;
    if (runtime()->playback_ == AnimationPlayback::stopped)
    {
        runtime()->previous_animation_.reset();
    }
    write_data<Data>()->animation_guid = std::move(guid);
    modified();
    runtime()->animation_ = std::move(candidate);
    runtime()->target_skeleton_ = std::move(target);
    ++runtime()->revision_;
    runtime()->time_ = 0.0;
    runtime()->fade_seconds_ = seconds;
    runtime()->fade_elapsed_ = 0.0;
    runtime()->playback_ = AnimationPlayback::playing;
    runtime()->awake_pending_ = false;
    runtime()->pose_dirty_ = true;
    return true;
}

std::string_view Animator::animation_path() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->animation_ ? std::string_view(runtime()->animation_->info().path)
                                 : std::string_view{};
}

bool Animator::play() noexcept
{
    if (!writable())
    {
        return false;
    }
    if (!active() || !runtime()->animation_ || !runtime()->animation_->animation())
    {
        return false;
    }
    if (runtime()->playback_ == AnimationPlayback::stopped)
    {
        runtime()->time_ = 0.0;
    }
    runtime()->playback_ = AnimationPlayback::playing;
    runtime()->awake_pending_ = false;
    runtime()->pose_dirty_ = true;
    return true;
}

bool Animator::pause() noexcept
{
    if (!writable())
    {
        return false;
    }
    if (runtime()->playback_ == AnimationPlayback::stopped)
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
    runtime()->previous_animation_.reset();
    runtime()->frozen_pose_.reset();
    runtime()->fade_seconds_ = 0.0;
    runtime()->fade_elapsed_ = 0.0;
    runtime()->playback_ = AnimationPlayback::stopped;
    runtime()->time_ = 0.0;
    runtime()->awake_pending_ = false;
    runtime()->pose_dirty_ = false;
    auto renderer = node().get_component<MeshRenderer>();
    if (renderer)
    {
        renderer.clear_deformation();
    }
    return true;
}

bool Animator::seek(double seconds) noexcept
{
    if (!writable())
    {
        return false;
    }
    const auto* animation = runtime()->animation_ ? runtime()->animation_->animation() : nullptr;
    if (!animation || !std::isfinite(seconds) || seconds < 0.0 || seconds > animation->duration)
    {
        return false;
    }
    runtime()->previous_animation_.reset();
    runtime()->frozen_pose_.reset();
    runtime()->fade_seconds_ = 0.0;
    runtime()->fade_elapsed_ = 0.0;
    runtime()->time_ = seconds;
    if (runtime()->playback_ == AnimationPlayback::stopped)
    {
        runtime()->playback_ = AnimationPlayback::paused;
    }
    runtime()->pose_dirty_ = true;
    runtime()->awake_pending_ = false;
    return true;
}

AnimationPlayback Animator::playback() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->playback_;
}

double Animator::time() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->time_;
}

} // namespace engine
