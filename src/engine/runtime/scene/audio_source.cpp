#include "runtime/scene/audio_source.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/resource_system.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace engine
{

AudioSource::AudioSource(Node& node) : Component(node)
{
    mutable_data().payload.Set(schema::AudioSourceDataT{});
}

bool AudioSource::valid_settings(const schema::AudioSourceDataT& settings) noexcept
{
    return (settings.clip_guid.empty() || GuidMap::valid_guid(settings.clip_guid)) &&
           std::isfinite(settings.volume) && settings.volume >= 0.0F && settings.volume <= 1.0F &&
           std::isfinite(settings.pitch) && settings.pitch >= 0.01F && settings.pitch <= 4.0F &&
           std::isfinite(settings.min_distance) && settings.min_distance >= 0.0F &&
           std::isfinite(settings.max_distance) && settings.max_distance > settings.min_distance;
}

const schema::AudioSourceDataT& AudioSource::settings() const noexcept
{
    return *data().payload.AsAudioSourceData();
}

bool AudioSource::set_settings(const schema::AudioSourceDataT& settings)
{
    if (!valid_settings(settings))
    {
        return false;
    }
    const bool changed_clip = settings.clip_guid != this->settings().clip_guid;
    ResourcePtr replacement = clip_;
    if (changed_clip)
    {
        if (revision_ == std::numeric_limits<std::uint64_t>::max())
        {
            return false;
        }
        replacement.reset();
        if (!settings.clip_guid.empty())
        {
            auto* resources = ResourceSystem::instance();
            if (resources == nullptr ||
                !resources->loader().load(settings.clip_guid, replacement) || !replacement ||
                replacement->audio() == nullptr)
            {
                return false;
            }
        }
    }
    *mutable_data().payload.AsAudioSourceData() = settings;
    clip_ = std::move(replacement);
    if (changed_clip)
    {
        ++revision_;
        playback_ = AudioPlayback::stopped;
    }
    return true;
}

bool AudioSource::restore_data(const schema::ComponentDataT& data)
{
    const auto* settings = data.payload.AsAudioSourceData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(data.enabled);
    return true;
}

bool AudioSource::set_clip(std::string_view path)
{
    schema::AudioSourceDataT replacement(settings());
    if (path.empty())
    {
        replacement.clip_guid.clear();
    }
    else if (!ResourceSystem::path_to_guid(path, replacement.clip_guid))
    {
        return false;
    }
    return set_settings(replacement);
}

std::string_view AudioSource::clip_path() const noexcept
{
    return clip_ ? std::string_view(clip_->info().path) : std::string_view{};
}

const schema::AudioClipDataT* AudioSource::clip_data() const noexcept
{
    return clip_ ? clip_->audio() : nullptr;
}

bool AudioSource::play() noexcept
{
    if (!enabled() || !clip_ ||
        (playback_ != AudioPlayback::paused &&
         revision_ == std::numeric_limits<std::uint64_t>::max()))
    {
        return false;
    }
    if (playback_ != AudioPlayback::paused)
    {
        ++revision_;
    }
    awake_pending_ = false;
    playback_ = AudioPlayback::pending;
    return true;
}

bool AudioSource::pause() noexcept
{
    if (playback_ != AudioPlayback::pending && playback_ != AudioPlayback::playing &&
        playback_ != AudioPlayback::paused)
    {
        return false;
    }
    awake_pending_ = false;
    playback_ = AudioPlayback::paused;
    return true;
}

bool AudioSource::stop() noexcept
{
    awake_pending_ = false;
    playback_ = AudioPlayback::stopped;
    return true;
}

bool AudioSource::playing() const noexcept
{
    return playback_ == AudioPlayback::playing;
}

AudioPlayback AudioSource::playback() const noexcept
{
    return playback_;
}

} // namespace engine
