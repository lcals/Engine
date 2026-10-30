#include "runtime/scene/audio_listener.hpp"

#include <cmath>

namespace engine
{

AudioListener::AudioListener(Node& node) : Component(node)
{
    mutable_data().payload.Set(schema::AudioListenerDataT{});
}

bool AudioListener::valid_settings(const schema::AudioListenerDataT& settings) noexcept
{
    return std::isfinite(settings.volume) && settings.volume >= 0.0F && settings.volume <= 1.0F;
}

const schema::AudioListenerDataT& AudioListener::settings() const noexcept
{
    return *data().payload.AsAudioListenerData();
}

bool AudioListener::set_settings(const schema::AudioListenerDataT& settings) noexcept
{
    if (!valid_settings(settings))
    {
        return false;
    }
    *mutable_data().payload.AsAudioListenerData() = settings;
    return true;
}

bool AudioListener::restore_data(const schema::ComponentDataT& data)
{
    const auto* settings = data.payload.AsAudioListenerData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(data.enabled);
    return true;
}

} // namespace engine
