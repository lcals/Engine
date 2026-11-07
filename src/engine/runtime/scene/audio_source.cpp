#include "runtime/scene/audio_source.hpp"

#include "runtime/resource/types/audio_resource.hpp"
#include "runtime/scene/audio_source_runtime.hpp"

#include "runtime/scene/ecs_storage.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/scene/resource_binding.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace engine
{

bool AudioSource::register_type(ComponentRegistry& registry)
{
    return register_component_storage<AudioSource, Data, Runtime>(registry);
}

AudioSource::Runtime* AudioSource::runtime() const noexcept
{
    return runtime_state<Runtime>();
}

bool AudioSource::valid_settings(const schema::AudioSourceDataT& settings) noexcept
{
    return (settings.clip_guid.empty() || GuidMap::valid_guid(settings.clip_guid)) &&
           std::isfinite(settings.volume) && settings.volume >= 0.0F && settings.volume <= 1.0F &&
           std::isfinite(settings.pitch) && settings.pitch >= 0.01F && settings.pitch <= 4.0F &&
           std::isfinite(settings.min_distance) && settings.min_distance >= 0.0F &&
           std::isfinite(settings.max_distance) && settings.max_distance > settings.min_distance;
}

schema::AudioSourceDataT AudioSource::settings() const noexcept
{
    Data output{};
    (void)get_settings(output);
    return output;
}

bool AudioSource::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool AudioSource::set_settings(const schema::AudioSourceDataT& settings)
{
    return apply_settings(settings, {});
}

bool AudioSource::apply_settings(const Data& settings, resource_detail::ResourceReference prepared)
{
    if (!writable())
    {
        return false;
    }
    if (!valid_settings(settings))
    {
        return false;
    }
    const bool changed_clip = settings.clip_guid != this->settings().clip_guid;
    resource_detail::ResourceReference replacement;
    if (runtime()->clip_ && !replacement.retain(runtime()->clip_))
    {
        return false;
    }
    if (changed_clip)
    {
        if (runtime()->revision_ == std::numeric_limits<std::uint64_t>::max())
        {
            return false;
        }
        replacement = std::move(prepared);
        if (!settings.clip_guid.empty())
        {
            auto* resources = node().world()->resource_loader();
            if (resources == nullptr ||
                (!replacement &&
                 !ecs_detail::acquire_resource(node(), settings.clip_guid, replacement)) ||
                !replacement || replacement.data<AudioResource>() == nullptr)
            {
                return false;
            }
        }
    }
    *write_data<Data>() = settings;
    modified();
    runtime()->clip_ = std::move(replacement);
    if (changed_clip)
    {
        ++runtime()->revision_;
        runtime()->playback_ = AudioPlayback::stopped;
    }
    return true;
}

bool AudioSource::restore_data(const schema::ComponentDataT& data)
{
    if (!data.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* settings = data.payload.AsAudioSourceData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    if (!restore_flags(data))
    {
        return false;
    }
    return true;
}

bool AudioSource::set_clip(std::string_view path)
{
    if (!writable())
    {
        return false;
    }
    resource_detail::ResourceReference prepared;
    schema::AudioSourceDataT replacement(settings());
    if (path.empty())
    {
        replacement.clip_guid.clear();
    }
    else if (!ecs_detail::acquire_resource(node(), path, prepared))
    {
        return false;
    }
    if (prepared)
    {
        replacement.clip_guid = prepared->info().guid;
    }
    return apply_settings(replacement, std::move(prepared));
}

std::string_view AudioSource::clip_path() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->clip_ ? std::string_view(runtime()->clip_->info().path) : std::string_view{};
}

const schema::AudioClipDataT* AudioSource::clip_data() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->clip_ ? runtime()->clip_.data<AudioResource>() : nullptr;
}

bool AudioSource::play() noexcept
{
    if (!writable())
    {
        return false;
    }
    if (!active() || !runtime()->clip_ ||
        (runtime()->playback_ != AudioPlayback::paused &&
         runtime()->revision_ == std::numeric_limits<std::uint64_t>::max()))
    {
        return false;
    }
    if (runtime()->playback_ != AudioPlayback::paused)
    {
        ++runtime()->revision_;
    }
    runtime()->awake_pending_ = false;
    runtime()->playback_ = AudioPlayback::pending;
    return true;
}

bool AudioSource::pause() noexcept
{
    if (!writable())
    {
        return false;
    }
    if (runtime()->playback_ != AudioPlayback::pending &&
        runtime()->playback_ != AudioPlayback::playing &&
        runtime()->playback_ != AudioPlayback::paused)
    {
        return false;
    }
    runtime()->awake_pending_ = false;
    runtime()->playback_ = AudioPlayback::paused;
    return true;
}

bool AudioSource::stop() noexcept
{
    if (!writable())
    {
        return false;
    }
    runtime()->awake_pending_ = false;
    runtime()->playback_ = AudioPlayback::stopped;
    return true;
}

bool AudioSource::playing() const noexcept
{
    if (!valid())
    {
        return false;
    }
    return active() && runtime()->playback_ == AudioPlayback::playing;
}

AudioPlayback AudioSource::playback() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->playback_;
}

} // namespace engine
