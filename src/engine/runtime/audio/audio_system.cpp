#include "runtime/audio/audio_system.hpp"

#include "runtime/scene/audio_listener.hpp"
#include "runtime/scene/audio_source.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/schema/audio_generated.h"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <new>
#include <string>

namespace engine
{
namespace
{

struct Listener
{
    std::array<float, 16> matrix{};
    float volume = 1.0F;
    bool available = false;
};

Listener find_listener(const Scene& scene)
{
    Listener result;
    for (const auto& node : scene.nodes())
    {
        const auto* listener = node->get_component<AudioListener>();
        const auto* transform = node->get_component<Transform>();
        if (listener && listener->enabled() && transform && transform->enabled() &&
            transform->world_matrix(result.matrix.data()))
        {
            result.volume = listener->settings().volume;
            result.available = true;
            break;
        }
    }
    return result;
}

bool spatial_mix(const AudioSource& source, const Listener& listener, float& gain, float& pan)
{
    gain = source.settings().volume * listener.volume;
    pan = 0.0F;
    if (!source.settings().spatial)
    {
        return true;
    }
    if (!listener.available)
    {
        gain = 0.0F;
        return true;
    }
    const auto* transform = source.node().get_component<Transform>();
    std::array<float, 16> matrix{};
    if (!transform || !transform->enabled() || !transform->world_matrix(matrix.data()))
    {
        return false;
    }
    const double x = static_cast<double>(matrix[12]) - listener.matrix[12];
    const double y = static_cast<double>(matrix[13]) - listener.matrix[13];
    const double z = static_cast<double>(matrix[14]) - listener.matrix[14];
    const double distance = std::sqrt(x * x + y * y + z * z);
    const auto& settings = source.settings();
    const double attenuation =
        std::clamp((static_cast<double>(settings.max_distance) - distance) /
                       (static_cast<double>(settings.max_distance) - settings.min_distance),
                   0.0, 1.0);
    gain *= static_cast<float>(attenuation);
    const double right_x = listener.matrix[0];
    const double right_y = listener.matrix[1];
    const double right_z = listener.matrix[2];
    const double right_length =
        std::sqrt(right_x * right_x + right_y * right_y + right_z * right_z);
    if (right_length <= 0.0)
    {
        return false;
    }
    if (distance > 0.0)
    {
        pan = static_cast<float>(std::clamp(
            (x * right_x + y * right_y + z * right_z) / (distance * right_length), -1.0, 1.0));
    }
    return true;
}

bool valid_clip(const schema::AudioClipDataT& clip) noexcept
{
    return clip.schema_version == 1 && clip.sample_rate >= 8000 && clip.sample_rate <= 192000 &&
           (clip.channels == 1 || clip.channels == 2) && clip.frames > 0 &&
           clip.frames == clip.samples.size() / clip.channels &&
           clip.samples.size() % clip.channels == 0;
}

} // namespace

struct AudioSystem::Impl
{
    struct Voice
    {
        Voice() noexcept = default;
        Voice(const Voice&) = delete;
        Voice& operator=(const Voice&) = delete;
        Voice(Voice&&) = delete;
        Voice& operator=(Voice&&) = delete;

        ~Voice() noexcept
        {
            if (stream)
            {
                SDL_DestroyAudioStream(stream);
            }
        }

        SDL_AudioStream* stream = nullptr;
        std::uint64_t revision = 0;
        std::uint64_t frame = 0;
        bool bound = false;
        bool flushed = false;
    };

    ~Impl()
    {
        close_device();
    }

    bool fail(std::string_view context)
    {
        error = std::string(context) + ": " + SDL_GetError();
        return false;
    }

    bool open_device()
    {
        if (device != 0)
        {
            return true;
        }
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            return fail("Cannot initialize SDL audio");
        }
        audio_initialized = true;
        const SDL_AudioSpec format{SDL_AUDIO_F32, 2, 48000};
        device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &format);
        if (device == 0)
        {
            const bool result = fail("Cannot open audio playback device");
            close_device();
            return result;
        }
        if (paused && !SDL_PauseAudioDevice(device))
        {
            const bool result = fail("Cannot pause audio playback device");
            close_device();
            return result;
        }
        return true;
    }

    void close_device() noexcept
    {
        voices.clear();
        if (device != 0)
        {
            SDL_CloseAudioDevice(device);
            device = 0;
        }
        if (audio_initialized)
        {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            audio_initialized = false;
        }
    }

    bool fill(Voice& voice, const AudioSource& source, const Listener& listener)
    {
        const auto* clip = source.clip_data();
        if (!clip || !valid_clip(*clip))
        {
            error = "Audio source has no valid PCM clip";
            return false;
        }
        float gain = 1.0F;
        float pan = 0.0F;
        if (!spatial_mix(source, listener, gain, pan))
        {
            error = "Audio source or listener world transform is invalid";
            return false;
        }
        if (!SDL_SetAudioStreamFrequencyRatio(voice.stream, source.settings().pitch) ||
            !SDL_SetAudioStreamGain(voice.stream, gain))
        {
            return fail("Cannot update audio stream settings");
        }
        int queued = SDL_GetAudioStreamQueued(voice.stream);
        if (queued < 0)
        {
            return fail("Cannot query audio stream queue");
        }
        constexpr std::size_t block_frames = 1024;
        constexpr int frame_bytes = static_cast<int>(2 * sizeof(float));
        const int target_bytes = static_cast<int>(std::ceil(static_cast<double>(clip->sample_rate) *
                                                            source.settings().pitch * 0.08)) *
                                 frame_bytes;
        std::array<float, block_frames * 2> block{};
        while (queued < target_bytes)
        {
            if (voice.frame == clip->frames)
            {
                if (!source.settings().loop)
                {
                    if (!voice.flushed && !SDL_FlushAudioStream(voice.stream))
                    {
                        return fail("Cannot finish audio stream");
                    }
                    voice.flushed = true;
                    break;
                }
                voice.frame = 0;
                voice.flushed = false;
            }
            const std::size_t count = static_cast<std::size_t>(
                std::min<std::uint64_t>(block_frames, clip->frames - voice.frame));
            const float left_gain = std::sqrt(0.5F * (1.0F - pan));
            const float right_gain = std::sqrt(0.5F * (1.0F + pan));
            for (std::size_t frame = 0; frame < count; ++frame)
            {
                const auto index = static_cast<std::size_t>(voice.frame + frame) * clip->channels;
                const float left = clip->samples[index];
                const float right = clip->channels == 2 ? clip->samples[index + 1] : left;
                if (source.settings().spatial)
                {
                    const float mono = (left + right) * 0.5F;
                    block[frame * 2] = mono * left_gain;
                    block[frame * 2 + 1] = mono * right_gain;
                }
                else
                {
                    block[frame * 2] = left;
                    block[frame * 2 + 1] = right;
                }
            }
            const int bytes = static_cast<int>(count) * frame_bytes;
            if (!SDL_PutAudioStreamData(voice.stream, block.data(), bytes))
            {
                return fail("Cannot queue audio samples");
            }
            voice.frame += count;
            queued += bytes;
        }
        return true;
    }

    std::map<std::uint64_t, std::unique_ptr<Voice>> voices;
    std::uint64_t scene_id = 0;
    SDL_AudioDeviceID device = 0;
    bool audio_initialized = false;
    bool paused = false;
    std::string error;
};

AudioSystem::AudioSystem(WorldSystem* world) noexcept : world_(world)
{
}

AudioSystem::~AudioSystem() noexcept
{
    shutdown();
}

bool AudioSystem::on_initialize(const EngineConfig&)
{
    impl_.reset(new (std::nothrow) Impl());
    return impl_ != nullptr;
}

bool AudioSystem::on_tick(double)
{
    if (!impl_)
    {
        return false;
    }
    auto* scene = world_ ? world_->scene() : nullptr;
    if (!scene || scene->id() != impl_->scene_id)
    {
        impl_->close_device();
        impl_->scene_id = scene ? scene->id() : 0;
    }
    if (!scene)
    {
        return true;
    }
    for (auto iterator = impl_->voices.begin(); iterator != impl_->voices.end();)
    {
        auto* node = scene->find_node(iterator->first);
        auto* source = node ? node->get_component<AudioSource>() : nullptr;
        if (!source || !source->enabled() || source->playback_ == AudioPlayback::stopped ||
            source->playback_ == AudioPlayback::failed ||
            source->revision_ != iterator->second->revision)
        {
            if (source && !source->enabled())
            {
                source->playback_ = AudioPlayback::stopped;
                source->awake_pending_ = false;
            }
            iterator = impl_->voices.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }
    const Listener listener = find_listener(*scene);
    bool success = true;
    for (const auto& node : scene->nodes())
    {
        auto* source = node->get_component<AudioSource>();
        if (!source)
        {
            continue;
        }
        if (!source->enabled())
        {
            source->playback_ = AudioPlayback::stopped;
            continue;
        }
        if (source->awake_pending_)
        {
            source->awake_pending_ = false;
            if (source->settings().play_on_awake && source->clip_data() && !source->play())
            {
                impl_->error = "Cannot request play-on-awake audio";
                source->playback_ = AudioPlayback::failed;
                success = false;
            }
        }
        auto found = impl_->voices.find(node->id());
        if (source->playback_ == AudioPlayback::paused)
        {
            if (found != impl_->voices.end() && found->second->bound)
            {
                SDL_UnbindAudioStream(found->second->stream);
                found->second->bound = false;
            }
            continue;
        }
        if (source->playback_ != AudioPlayback::pending &&
            source->playback_ != AudioPlayback::playing)
        {
            continue;
        }
        const auto fail_source = [&]()
        {
            impl_->voices.erase(node->id());
            source->playback_ = AudioPlayback::failed;
            success = false;
        };
        const auto* clip = source->clip_data();
        if (!clip || !valid_clip(*clip))
        {
            impl_->error = "Audio source has no valid PCM clip";
            fail_source();
            continue;
        }
        if (!impl_->open_device())
        {
            fail_source();
            continue;
        }
        if (found == impl_->voices.end())
        {
            auto voice = std::make_unique<Impl::Voice>();
            const SDL_AudioSpec input{SDL_AUDIO_F32, 2, static_cast<int>(clip->sample_rate)};
            const SDL_AudioSpec output{SDL_AUDIO_F32, 2, 48000};
            voice->stream = SDL_CreateAudioStream(&input, &output);
            voice->revision = source->revision_;
            if (!voice->stream)
            {
                static_cast<void>(impl_->fail("Cannot create audio stream"));
                fail_source();
                continue;
            }
            found = impl_->voices.emplace(node->id(), std::move(voice)).first;
        }
        auto& voice = *found->second;
        if (!impl_->fill(voice, *source, listener))
        {
            fail_source();
            continue;
        }
        if (!voice.bound)
        {
            if (!SDL_BindAudioStream(impl_->device, voice.stream))
            {
                static_cast<void>(impl_->fail("Cannot bind audio stream"));
                fail_source();
                continue;
            }
            voice.bound = true;
        }
        const int available = SDL_GetAudioStreamAvailable(voice.stream);
        const int queued = SDL_GetAudioStreamQueued(voice.stream);
        if (available < 0 || queued < 0)
        {
            static_cast<void>(impl_->fail("Cannot query audio playback progress"));
            fail_source();
            continue;
        }
        if (voice.flushed && available == 0 && queued == 0)
        {
            source->playback_ = AudioPlayback::stopped;
            impl_->voices.erase(found);
        }
        else
        {
            source->playback_ = AudioPlayback::playing;
        }
    }
    if (success)
    {
        impl_->error.clear();
    }
    return success;
}

void AudioSystem::clear() noexcept
{
    if (!impl_)
    {
        return;
    }
    if (auto* scene = world_ ? world_->scene() : nullptr)
    {
        for (const auto& node : scene->nodes())
        {
            if (auto* source = node->get_component<AudioSource>())
            {
                source->playback_ = AudioPlayback::stopped;
                source->awake_pending_ = false;
            }
        }
    }
    impl_->close_device();
    impl_->scene_id = 0;
    impl_->error.clear();
}

bool AudioSystem::set_paused(bool paused)
{
    if (!impl_ || !is_initialized())
    {
        return false;
    }
    if (impl_->device != 0 &&
        !(paused ? SDL_PauseAudioDevice(impl_->device) : SDL_ResumeAudioDevice(impl_->device)))
    {
        return impl_->fail("Cannot change audio device pause state");
    }
    impl_->paused = paused;
    return true;
}

bool AudioSystem::paused() const noexcept
{
    return impl_ && impl_->paused;
}

std::size_t AudioSystem::voice_count() const noexcept
{
    return impl_ ? impl_->voices.size() : 0;
}

std::string_view AudioSystem::last_error() const noexcept
{
    return impl_ ? std::string_view(impl_->error) : std::string_view{};
}

void AudioSystem::on_shutdown() noexcept
{
    clear();
    impl_.reset();
}

} // namespace engine
