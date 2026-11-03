#include "runtime/audio/audio_system.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"

#include "runtime/performance/profiling.hpp"
#include "runtime/resource/resource.hpp"
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
    std::uint64_t selected_order = UINT64_MAX;
    auto& world = scene.world();
    auto query =
        flecs::world(world.native_world())
            .query_builder<const ecs_detail::ComponentStorage<AudioListener::Data>,
                           const ecs_detail::ComponentStorage<Transform::Data>,
                           const ecs_detail::NodeRuntime, const ecs_detail::WorldTransform>()
            .with<ecs_detail::Published>()
            .build();
    query.each(
        [&](const auto& listener, const auto& transform, const ecs_detail::NodeRuntime& state,
            const ecs_detail::WorldTransform& pose)
        {
            if (state.scene != &scene || !state.active || !listener.state.flags.enabled ||
                !transform.state.flags.enabled || !pose.valid || state.order >= selected_order)
            {
                return;
            }
            selected_order = state.order;
            result.matrix = pose.matrix;
            result.volume = listener.data.volume;
            result.available = true;
        });
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
    const auto transform = source.node().get_component<Transform>();
    std::array<float, 16> matrix{};
    if (!transform || !transform.active() || !transform.world_matrix(matrix.data()))
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

        static void SDLCALL feed(void* userdata, SDL_AudioStream* stream, int additional_bytes, int)
        {
            ENGINE_PROFILE_ZONE("Audio.feed");
            auto& voice = *static_cast<Voice*>(userdata);
            if (additional_bytes <= 0 || voice.feed_failed)
            {
                return;
            }
            // SDL 已持有流锁；这里只消费驻留 PCM 和已发布参数，不访问场景。
            const auto& clip = *voice.clip->audio();
            constexpr std::size_t block_frames = 1024;
            constexpr std::size_t frame_bytes = 2 * sizeof(float);
            std::size_t requested =
                (static_cast<std::size_t>(additional_bytes) + frame_bytes - 1) / frame_bytes;
            std::array<float, block_frames * 2> block;
            while (requested != 0)
            {
                if (voice.frame == clip.frames)
                {
                    if (!voice.loop)
                    {
                        break;
                    }
                    voice.frame = 0;
                    voice.flushed = false;
                }
                const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
                    std::min(block_frames, requested), clip.frames - voice.frame));
                for (std::size_t frame = 0; frame < count; ++frame)
                {
                    const auto index =
                        static_cast<std::size_t>(voice.frame + frame) * clip.channels;
                    const float left = clip.samples[index];
                    const float right = clip.channels == 2 ? clip.samples[index + 1] : left;
                    if (voice.spatial)
                    {
                        const float mono = (left + right) * 0.5F;
                        block[frame * 2] = mono * voice.left_gain;
                        block[frame * 2 + 1] = mono * voice.right_gain;
                    }
                    else
                    {
                        block[frame * 2] = left;
                        block[frame * 2 + 1] = right;
                    }
                }
                if (!SDL_PutAudioStreamData(stream, block.data(),
                                            static_cast<int>(count * frame_bytes)))
                {
                    voice.feed_failed = true;
                    return;
                }
                voice.frame += count;
                requested -= count;
            }
            if (voice.frame == clip.frames && !voice.loop && !voice.flushed)
            {
                voice.flushed = SDL_FlushAudioStream(stream);
                voice.feed_failed = !voice.flushed;
            }
        }

        SDL_AudioStream* stream = nullptr;
        ResourcePtr clip;
        std::uint64_t instance_id = 0;
        std::uint64_t revision = 0;
        std::uint64_t frame = 0;
        float left_gain = 1.0F;
        float right_gain = 1.0F;
        bool loop = false;
        bool spatial = false;
        bool bound = false;
        bool flushed = false;
        bool feed_failed = false;
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

    bool publish(Voice& voice, const AudioSource& source, const Listener& listener, bool& finished)
    {
        ENGINE_PROFILE_ZONE("Audio.publish");
        finished = false;
        float gain = 1.0F;
        float pan = 0.0F;
        if (!spatial_mix(source, listener, gain, pan))
        {
            error = "Audio source or listener world transform is invalid";
            return false;
        }
        const float left_gain = std::sqrt(0.5F * (1.0F - pan));
        const float right_gain = std::sqrt(0.5F * (1.0F + pan));
        if (!SDL_LockAudioStream(voice.stream))
        {
            return fail("Cannot lock audio stream settings");
        }
        // 与 SDL 回调共用流锁，只发布标量参数并查询状态，不在锁内生成 PCM。
        voice.loop = source.settings().loop;
        voice.spatial = source.settings().spatial;
        voice.left_gain = left_gain;
        voice.right_gain = right_gain;
        const bool updated =
            SDL_SetAudioStreamFrequencyRatio(voice.stream, source.settings().pitch) &&
            SDL_SetAudioStreamGain(voice.stream, gain);
        const int available = SDL_GetAudioStreamAvailable(voice.stream);
        const int queued = SDL_GetAudioStreamQueued(voice.stream);
        const bool feed_failed = voice.feed_failed;
        finished = voice.flushed && !voice.loop && available == 0 && queued == 0;
        if (!SDL_UnlockAudioStream(voice.stream))
        {
            return fail("Cannot unlock audio stream settings");
        }
        if (!updated || available < 0 || queued < 0)
        {
            return fail("Cannot update audio stream settings or query playback progress");
        }
        if (feed_failed)
        {
            // SDL 错误按线程保存，不能从主线程读取音频回调线程的错误字符串。
            error = "Audio callback could not queue or finish PCM samples";
            return false;
        }
        return true;
    }

    std::map<std::uint64_t, std::unique_ptr<Voice>> voices;
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
    ENGINE_PROFILE_ZONE("Audio.tick");
    if (!impl_)
    {
        return false;
    }
    if (world_ == nullptr || world_->scenes().empty())
    {
        impl_->close_device();
        return true;
    }
    if (!world_->refresh_scene_state())
    {
        return false;
    }
    for (auto iterator = impl_->voices.begin(); iterator != impl_->voices.end();)
    {
        auto node = world_->find_node(iterator->first);
        auto source = node ? node.get_component<AudioSource>() : AudioSource{};
        if (!source || source.runtime()->playback_ == AudioPlayback::stopped ||
            source.runtime()->playback_ == AudioPlayback::failed ||
            source.runtime()->revision_ != iterator->second->revision ||
            source.instance_id() != iterator->second->instance_id)
        {
            iterator = impl_->voices.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }
    Listener listener;
    if (const auto* scene = world_->scene())
    {
        listener = find_listener(*scene);
    }
    for (const auto& scene : world_->scenes())
    {
        if (!listener.available)
        {
            listener = find_listener(*scene);
        }
    }
    bool success = true;
    {
        const auto process =
            [&](Node node, const ecs_detail::ComponentStorage<AudioSource::Data>& stored) -> bool
        {
            const auto* scene = node.scene();
            (void)scene;
            (void)stored;

            auto source = node.get_component<AudioSource>();
            if (!source)
            {
                return true;
            }
            if (!source.active())
            {
                const auto voice = impl_->voices.find(node.id());
                if (voice != impl_->voices.end() && voice->second->bound)
                {
                    SDL_UnbindAudioStream(voice->second->stream);
                    voice->second->bound = false;
                }
                return true;
            }
            if (source.runtime()->awake_pending_)
            {
                source.runtime()->awake_pending_ = false;
                if (stored.data.play_on_awake && source.clip_data() && !source.play())
                {
                    impl_->error = "Cannot request play-on-awake audio";
                    source.runtime()->playback_ = AudioPlayback::failed;
                    success = false;
                }
            }
            auto found = impl_->voices.find(node.id());
            if (source.runtime()->playback_ == AudioPlayback::paused)
            {
                if (found != impl_->voices.end() && found->second->bound)
                {
                    SDL_UnbindAudioStream(found->second->stream);
                    found->second->bound = false;
                }
                return true;
            }
            if (source.runtime()->playback_ != AudioPlayback::pending &&
                source.runtime()->playback_ != AudioPlayback::playing)
            {
                return true;
            }
            const auto fail_source = [&]()
            {
                impl_->voices.erase(node.id());
                source.runtime()->playback_ = AudioPlayback::failed;
                success = false;
            };
            const auto* clip = source.clip_data();
            if (!clip || !valid_clip(*clip))
            {
                impl_->error = "Audio source has no valid PCM clip";
                fail_source();
                return true;
            }
            if (!impl_->open_device())
            {
                fail_source();
                return true;
            }
            if (found == impl_->voices.end())
            {
                auto voice = std::make_unique<Impl::Voice>();
                const SDL_AudioSpec input{SDL_AUDIO_F32, 2, static_cast<int>(clip->sample_rate)};
                const SDL_AudioSpec output{SDL_AUDIO_F32, 2, 48000};
                voice->stream = SDL_CreateAudioStream(&input, &output);
                voice->clip = source.runtime()->clip_;
                voice->instance_id = source.instance_id();
                voice->revision = source.runtime()->revision_;
                if (!voice->stream ||
                    !SDL_SetAudioStreamGetCallback(voice->stream, Impl::Voice::feed, voice.get()))
                {
                    static_cast<void>(impl_->fail("Cannot create audio stream"));
                    fail_source();
                    return true;
                }
                found = impl_->voices.emplace(node.id(), std::move(voice)).first;
            }
            auto& voice = *found->second;
            bool finished = false;
            if (!impl_->publish(voice, source, listener, finished))
            {
                fail_source();
                return true;
            }
            if (!finished && !voice.bound)
            {
                if (!SDL_BindAudioStream(impl_->device, voice.stream))
                {
                    static_cast<void>(impl_->fail("Cannot bind audio stream"));
                    fail_source();
                    return true;
                }
                voice.bound = true;
            }
            if (finished)
            {
                source.runtime()->playback_ = AudioPlayback::stopped;
                impl_->voices.erase(found);
            }
            else
            {
                source.runtime()->playback_ = AudioPlayback::playing;
            }

            return true;
        };
        auto query = flecs::world(world_->native_world())
                         .query_builder<const ecs_detail::ComponentStorage<AudioSource::Data>>()
                         .with<ecs_detail::Published>()
                         .build();
        bool query_success = true;
        query.each(
            [&](flecs::entity entity, const ecs_detail::ComponentStorage<AudioSource::Data>& stored)
            {
                if (query_success)
                {
                    query_success = process(world_->find_node(entity.id()), stored);
                }
            });
        (void)query_success;
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
    if (world_ != nullptr)
    {
        {
            const auto process =
                [&](Node node,
                    const ecs_detail::ComponentStorage<AudioSource::Data>& stored) -> bool
            {
                const auto* scene = node.scene();
                (void)scene;
                (void)stored;

                if (auto source = node.get_component<AudioSource>())
                {
                    source.runtime()->playback_ = AudioPlayback::stopped;
                    source.runtime()->awake_pending_ = false;
                }

                return true;
            };
            auto query = flecs::world(world_->native_world())
                             .query_builder<const ecs_detail::ComponentStorage<AudioSource::Data>>()
                             .with<ecs_detail::Published>()
                             .build();
            bool query_success = true;
            query.each(
                [&](flecs::entity entity,
                    const ecs_detail::ComponentStorage<AudioSource::Data>& stored)
                {
                    if (query_success)
                    {
                        query_success = process(world_->find_node(entity.id()), stored);
                    }
                });
            (void)query_success;
        }
    }
    impl_->close_device();
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
