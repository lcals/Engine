#include "runtime/movie/movie_player.hpp"

#include "runtime/audio/audio_system.hpp"
#include "runtime/resource/resource_reference.hpp"
#include "runtime/resource/types/audio_resource.hpp"
#include "runtime/resource/types/movie_resource.hpp"
#include "runtime/task/task_system.hpp"

#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <utility>
#include <vector>

namespace engine
{
namespace
{
std::atomic<std::uint64_t> next_generation{1};

std::uint32_t read32(const std::uint8_t* p) noexcept
{
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) |
           (std::uint32_t{p[3]} << 24);
}

struct DecodedFrame
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t index = 0;
    std::array<std::vector<std::uint8_t>, 3> planes;
};

struct Decoder
{
    explicit Decoder(const schema::MovieDataT& source) : resource(source)
    {
    }
    ~Decoder() noexcept
    {
        if (initialized)
        {
            (void)vpx_codec_destroy(&codec);
        }
    }
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    bool decode(bool publish)
    {
        const auto* video = resource.video.get();
        const auto* payload = &video->payload;
        if (!initialized)
        {
            vpx_codec_dec_cfg_t config{};
            config.threads = 1;
            if (vpx_codec_dec_init(&codec, vpx_codec_vp9_dx(), &config, 0) != VPX_CODEC_OK)
            {
                error = "cannot initialize VP9 decoder";
                return false;
            }
            initialized = true;
        }
        if (index >= video->frame_count || offset > payload->size() ||
            payload->size() - offset < 12)
        {
            error = "missing video frame";
            return false;
        }
        const std::uint32_t size = read32(payload->data() + offset);
        offset += 12;
        if (size > payload->size() - offset ||
            vpx_codec_decode(&codec, payload->data() + offset, size, nullptr, 0) != VPX_CODEC_OK)
        {
            error = "invalid VP9 frame";
            return false;
        }
        offset += size;
        vpx_codec_iter_t iterator = nullptr;
        const vpx_image_t* image = vpx_codec_get_frame(&codec, &iterator);
        if (!image || image->fmt != VPX_IMG_FMT_I420 || image->d_w != video->width ||
            image->d_h != video->height)
        {
            error = "unsupported VP9 output format or frame size";
            return false;
        }
        DecodedFrame frame;
        frame.width = image->d_w;
        frame.height = image->d_h;
        frame.index = index++;
        for (std::size_t plane = 0; plane < 3; ++plane)
        {
            const std::uint32_t width = plane == 0 ? frame.width : (frame.width + 1) / 2;
            const std::uint32_t height = plane == 0 ? frame.height : (frame.height + 1) / 2;
            if (!image->planes[plane] || image->stride[plane] < static_cast<int>(width))
            {
                error = "invalid VP9 output stride";
                return false;
            }
            if (!publish)
            {
                continue;
            }
            auto& destination = frame.planes[plane];
            destination.resize(static_cast<std::size_t>(width) * height);
            for (std::uint32_t row = 0; row < height; ++row)
            {
                std::memcpy(destination.data() + static_cast<std::size_t>(row) * width,
                            image->planes[plane] +
                                static_cast<std::size_t>(row) * image->stride[plane],
                            width);
            }
        }
        if (vpx_codec_get_frame(&codec, &iterator))
        {
            error = "multiple output frames in a VP9 packet";
            return false;
        }
        if (publish)
        {
            output = std::move(frame);
        }
        return true;
    }

    bool decode_until(std::uint32_t target)
    {
        while (index <= target)
        {
            if (cancelled.load(std::memory_order_relaxed) || !decode(index == target))
            {
                return false;
            }
        }
        return true;
    }

    const schema::MovieDataT& resource;
    vpx_codec_ctx_t codec{};
    bool initialized = false;
    std::size_t offset = 32;
    std::uint32_t index = 0;
    DecodedFrame output;
    std::string error;
    std::atomic<bool> cancelled{false};
};
} // namespace

struct MoviePlayer::Impl
{
    Impl(ResourceLoader& resource_system, TaskSystem& task_system, AudioSystem& audio_system)
        : resources(resource_system), tasks(task_system), audio(audio_system),
          generation(next_generation.fetch_add(1, std::memory_order_relaxed))
    {
    }
    bool valid(MoviePlaybackHandle handle) const noexcept
    {
        return active.id != 0 && handle.id == active.id && handle.generation == active.generation;
    }
    bool terminal() const noexcept
    {
        return state.state == MovieState::completed || state.state == MovieState::cancelled ||
               state.state == MovieState::failed;
    }
    void close_audio() noexcept
    {
        if (!voice.id)
        {
            return;
        }
        AudioPlaybackStatus playback;
        if (audio.playback(voice, playback) && (playback.state == AudioVoiceState::playing ||
                                                playback.state == AudioVoiceState::paused))
        {
            (void)audio.stop(voice);
        }
        (void)audio.release(voice);
        voice = {};
    }
    void cancel_decode() noexcept
    {
        if (decoder)
        {
            decoder->cancelled.store(true, std::memory_order_relaxed);
        }
        if (pending.valid())
        {
            retired.push_back({std::move(movie), std::move(decoder), std::move(pending)});
        }
        else
        {
            decoder.reset();
        }
        movie.reset();
        clip.reset();
        collect();
    }
    void fail(std::string message)
    {
        close_audio();
        state.error = std::move(message);
        state.state = MovieState::failed;
        current = {};
        next = {};
        has_current = false;
        has_next = false;
        cancel_decode();
    }
    bool schedule(std::uint32_t target)
    {
        auto job = decoder;
        pending =
            tasks.submit("decode movie frame", [job, target] { return job->decode_until(target); });
        if (!pending.valid())
        {
            fail("cannot submit movie decode task");
            return false;
        }
        return true;
    }
    ResourceLoader& resources;
    TaskSystem& tasks;
    AudioSystem& audio;
    std::uint64_t generation = 0;
    std::uint64_t serial = 0;
    MoviePlaybackHandle active{};
    MoviePlaybackStatus state{};
    std::string error;
    resource_detail::ResourceReference movie;
    resource_detail::ResourceReference clip;
    struct Retired
    {
        resource_detail::ResourceReference movie;
        std::shared_ptr<Decoder> decoder;
        std::future<bool> pending;
    };
    std::vector<Retired> retired;
    void collect() noexcept
    {
        std::erase_if(
            retired, [](auto& job)
            { return job.pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
    }
    std::shared_ptr<Decoder> decoder;
    std::future<bool> pending;
    DecodedFrame current;
    DecodedFrame next;
    bool has_current = false;
    bool has_next = false;
    AudioVoiceHandle voice{};
};

MoviePlayer::MoviePlayer(ResourceLoader& resources, TaskSystem& tasks, AudioSystem& audio) noexcept
    : impl_(std::make_unique<Impl>(resources, tasks, audio))
{
}
MoviePlayer::~MoviePlayer() noexcept
{
    impl_->close_audio();
    impl_->cancel_decode();
    for (auto& job : impl_->retired)
    {
        job.pending.wait();
    }
    impl_->retired.clear();
}

bool MoviePlayer::play(const ResourceHandle& handle, MoviePlaybackHandle& output)
{
    output = {};
    auto& p = *impl_;
    p.collect();
    p.error.clear();
    if (p.active.id || p.serial == UINT64_MAX || !p.tasks.is_initialized() ||
        !p.audio.is_initialized())
    {
        p.error = "Movie player is busy or services are not ready";
        return false;
    }
    resource_detail::ResourceReference movie, clip;
    if (!movie.retain(&p.resources, handle) || !movie.data<MovieResource>())
    {
        p.error = "Movie resource is not loaded or has the wrong type";
        return false;
    }
    const auto& data = *movie.data<MovieResource>();
    if (!data.audio_guid.empty() &&
        (!clip.dependency(&p.resources, movie.handle(), data.audio_guid) ||
         !clip.data<AudioResource>()))
    {
        p.error = "Movie audio dependency is not available";
        return false;
    }
    p.cancel_decode();
    p.movie = std::move(movie);
    p.clip = std::move(clip);
    p.active = {++p.serial, p.generation};
    p.state = {};
    p.has_current = false;
    p.has_next = false;
    output = p.active;
    return true;
}

bool MoviePlayer::set_paused(MoviePlaybackHandle handle, bool paused)
{
    auto& p = *impl_;
    if (!p.valid(handle) ||
        (p.state.state != MovieState::playing && p.state.state != MovieState::paused))
    {
        return false;
    }
    if ((p.state.state == MovieState::paused) == paused)
    {
        return true;
    }
    if (p.voice.id)
    {
        AudioPlaybackStatus playback;
        if (!p.audio.playback(p.voice, playback) ||
            (playback.state != AudioVoiceState::completed && !p.audio.set_paused(p.voice, paused) &&
             (!p.audio.playback(p.voice, playback) ||
              playback.state != AudioVoiceState::completed)))
        {
            p.fail("cannot pause or resume movie audio");
            return false;
        }
        if (paused)
        {
            if (!p.audio.playback(p.voice, playback))
            {
                p.fail("cannot query paused movie audio");
                return false;
            }
            p.state.position_seconds = playback.state == AudioVoiceState::completed
                                           ? p.state.duration_seconds
                                           : playback.position_seconds;
        }
    }
    p.state.state = paused ? MovieState::paused : MovieState::playing;
    return true;
}

bool MoviePlayer::stop(MoviePlaybackHandle handle, MovieStopReason reason)
{
    auto& p = *impl_;
    if (!p.valid(handle) || p.terminal() ||
        (reason != MovieStopReason::stopped && reason != MovieStopReason::skipped))
    {
        return false;
    }
    p.close_audio();
    p.state.state = MovieState::cancelled;
    p.state.stop_reason = reason;
    p.cancel_decode();
    p.has_current = false;
    p.has_next = false;
    return true;
}

bool MoviePlayer::status(MoviePlaybackHandle handle, MoviePlaybackStatus& output) const
{
    output = {};
    if (!impl_->valid(handle))
    {
        return false;
    }
    output = impl_->state;
    return true;
}

bool MoviePlayer::current_handle(MoviePlaybackHandle& output) const noexcept
{
    output = impl_->active;
    return output.id != 0;
}

bool MoviePlayer::release(MoviePlaybackHandle handle)
{
    auto& p = *impl_;
    if (!p.valid(handle) || !p.terminal())
    {
        return false;
    }
    p.close_audio();
    p.active = {};
    p.cancel_decode();
    p.current = {};
    p.next = {};
    p.has_current = false;
    p.has_next = false;
    return true;
}

bool MoviePlayer::update(double delta_time)
{
    auto& p = *impl_;
    p.collect();
    if (!std::isfinite(delta_time) || delta_time < 0 || !p.tasks.is_initialized() ||
        !p.audio.is_initialized())
    {
        p.error = "invalid movie update time or services are not ready";
        return false;
    }
    if (!p.active.id || p.terminal() || p.state.state == MovieState::paused)
    {
        return true;
    }
    if (p.state.state == MovieState::preparing && !p.decoder)
    {
        const auto* movie = p.movie.data<MovieResource>();
        if (!movie || !movie->video)
        {
            p.fail("Movie resource is no longer available");
            return true;
        }
        const auto& video = *movie->video;
        p.state.duration_seconds =
            static_cast<double>(video.frame_count) * video.frame_rate_den / video.frame_rate_num;
        p.decoder = std::make_shared<Decoder>(*movie);
        if (!p.schedule(0))
        {
            return true;
        }
    }
    if (p.pending.valid() &&
        p.pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        if (!p.pending.get())
        {
            p.fail(p.decoder->error);
            return true;
        }
        p.next = std::move(p.decoder->output);
        p.has_next = true;
    }
    if (p.state.state == MovieState::preparing && p.has_next)
    {
        p.current = std::move(p.next);
        p.has_current = true;
        p.has_next = false;
        if (p.clip && !p.audio.play(p.clip.handle(), p.voice))
        {
            p.fail(std::string(p.audio.last_error()));
            return true;
        }
        p.state.state = MovieState::playing;
        delta_time = 0;
    }
    if (p.state.state != MovieState::playing)
    {
        return true;
    }
    bool audio_finished = !p.voice.id;
    if (p.voice.id)
    {
        AudioPlaybackStatus audio;
        if (!p.audio.playback(p.voice, audio) || audio.state == AudioVoiceState::failed ||
            audio.state == AudioVoiceState::stopped)
        {
            p.fail("movie audio playback failed");
            return true;
        }
        p.state.position_seconds = audio.position_seconds;
        audio_finished = audio.state == AudioVoiceState::completed;
        if (audio_finished)
        {
            p.state.position_seconds = p.state.duration_seconds;
        }
    }
    else
    {
        p.state.position_seconds =
            std::min(p.state.duration_seconds, p.state.position_seconds + delta_time);
    }
    const auto* video = p.movie.data<MovieResource>()->video.get();
    const double next_time = p.has_next ? static_cast<double>(p.next.index) *
                                              video->frame_rate_den / video->frame_rate_num
                                        : 0;
    if (p.has_next && p.state.position_seconds >= next_time)
    {
        p.current = std::move(p.next);
        p.has_next = false;
    }
    if (!p.has_next && !p.pending.valid() && p.decoder->index < video->frame_count)
    {
        const double frame_position =
            p.state.position_seconds * video->frame_rate_num / video->frame_rate_den;
        const auto target = static_cast<std::uint32_t>(
            std::min(frame_position, static_cast<double>(video->frame_count - 1)));
        if (!p.schedule(std::max(target, p.decoder->index)))
        {
            return true;
        }
    }
    if (audio_finished && p.state.position_seconds >= p.state.duration_seconds &&
        p.current.index + 1 == video->frame_count && !p.pending.valid() && !p.has_next)
    {
        p.state.state = MovieState::completed;
    }
    return true;
}

bool MoviePlayer::frame(MoviePlaybackHandle handle, MovieFrame& output) const
{
    output = {};
    const auto& p = *impl_;
    if (!p.valid(handle) || !p.has_current || !p.movie ||
        (p.state.state != MovieState::playing && p.state.state != MovieState::paused &&
         p.state.state != MovieState::completed))
    {
        return false;
    }
    const auto* video = p.movie.data<MovieResource>()->video.get();
    output.width = p.current.width;
    output.height = p.current.height;
    output.presentation_ticks = p.current.index;
    output.time_base_num = video->time_base_num;
    output.time_base_den = video->time_base_den;
    output.color = *video->color;
    for (std::size_t plane = 0; plane < 3; ++plane)
    {
        const auto width = plane == 0 ? output.width : (output.width + 1) / 2;
        const auto height = plane == 0 ? output.height : (output.height + 1) / 2;
        output.planes[plane] = {p.current.planes[plane], width, height, width};
    }
    return true;
}
std::string_view MoviePlayer::last_error() const noexcept
{
    return impl_->error;
}
} // namespace engine
