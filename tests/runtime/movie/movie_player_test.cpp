#include "resource_fixture.hpp"

#include "runtime/audio/audio_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/movie/movie_player.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <vpx/vp8cx.h>
#include <vpx/vpx_encoder.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>
#include <vector>

namespace
{
using namespace engine;
bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "movie player: %s\n", message);
    }
    return value;
}
void le(std::vector<std::uint8_t>& bytes, std::uint64_t value, unsigned int width)
{
    for (unsigned int index = 0; index < width; ++index)
    {
        bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8)));
    }
}
bool make_movie(test_support::ResourceFixture& fixture)
{
    vpx_codec_enc_cfg_t config{};
    if (vpx_codec_enc_config_default(vpx_codec_vp9_cx(), &config, 0) != VPX_CODEC_OK)
    {
        return false;
    }
    config.g_w = 8;
    config.g_h = 8;
    config.g_timebase = {1, 10};
    config.g_lag_in_frames = 0;
    config.g_threads = 1;
    vpx_codec_ctx_t encoder{};
    if (vpx_codec_enc_init(&encoder, vpx_codec_vp9_cx(), &config, 0) != VPX_CODEC_OK)
    {
        return false;
    }
    vpx_image_t image{};
    if (!vpx_img_alloc(&image, VPX_IMG_FMT_I420, 8, 8, 1))
    {
        (void)vpx_codec_destroy(&encoder);
        return false;
    }
    std::vector<std::uint8_t> ivf{'D','K','I','F',0,0,32,0,'V','P','9','0'};
    le(ivf, 8, 2);
    le(ivf, 8, 2);
    le(ivf, 10, 4);
    le(ivf, 1, 4);
    le(ivf, 3, 4);
    le(ivf, 0, 4);
    bool success = true;
    for (int frame = 0; frame < 3 && success; ++frame)
    {
        for (int plane = 0; plane < 3; ++plane)
        {
            const int size = plane == 0 ? 8 : 4;
            for (int y = 0; y < size; ++y)
            {
                for (int x = 0; x < size; ++x)
                {
                    image.planes[plane][y * image.stride[plane] + x] =
                        static_cast<unsigned char>(plane == 0 ? 40 + frame * 50 : 128);
                }
            }
        }
        if (vpx_codec_encode(&encoder, &image, frame, 1, 0, VPX_DL_REALTIME) != VPX_CODEC_OK)
        {
            success = false;
            break;
        }
        vpx_codec_iter_t iterator = nullptr;
        const vpx_codec_cx_pkt_t* packet = nullptr;
        unsigned int count = 0;
        while ((packet = vpx_codec_get_cx_data(&encoder, &iterator)) != nullptr)
        {
            if (packet->kind != VPX_CODEC_CX_FRAME_PKT)
            {
                continue;
            }
            const auto size = packet->data.frame.sz;
            const auto* first = static_cast<const std::uint8_t*>(packet->data.frame.buf);
            le(ivf, size, 4);
            le(ivf, frame, 8);
            ivf.insert(ivf.end(), first, first + size);
            ++count;
        }
        success = count == 1;
    }
    vpx_img_free(&image);
    (void)vpx_codec_destroy(&encoder);
    if (!success)
    {
        return false;
    }
    schema::MovieDataT movie;
    movie.video = std::make_unique<schema::MovieVideoDataT>();
    movie.video->width = 8;
    movie.video->height = 8;
    movie.video->frame_rate_num = 10;
    movie.video->frame_rate_den = 1;
    movie.video->time_base_num = 1;
    movie.video->time_base_den = 10;
    movie.video->frame_count = 3;
    movie.video->duration_ticks = 3;
    movie.video->color = std::make_unique<schema::MovieColorInfo>();
    movie.video->payload = std::move(ivf);
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishMovieDataBuffer(builder, schema::MovieData::Pack(builder, &movie));
    auto add = [&](std::string path, std::string guid, schema::AssetKind kind,
                   const flatbuffers::FlatBufferBuilder& buffer,
                   std::vector<std::string> dependencies = {})
    {
        auto entry = std::make_unique<schema::AssetEntryT>();
        entry->guid = std::move(guid);
        entry->path = std::move(path);
        entry->kind = kind;
        entry->dependencies = std::move(dependencies);
        ByteBuffer entity;
        if (!ResourceSystem::encode_resource(entry->guid, entry->kind,
            {reinterpret_cast<const std::byte*>(buffer.GetBufferPointer()), buffer.GetSize()},
            entity, entry->header_size))
        {
            return false;
        }
        std::uint64_t hash = 14695981039346656037ULL;
        for (const auto byte : entity)
        {
            hash = (hash ^ std::to_integer<std::uint8_t>(byte)) * 1099511628211ULL;
        }
        char digest[17]{};
        std::snprintf(digest, sizeof(digest), "%016llx", static_cast<unsigned long long>(hash));
        entry->content_hash = digest;
        entry->content_size = entity.size();
        entry->location = std::make_unique<schema::FileLocationT>();
        entry->location->mount = "content";
        entry->location->path = "gen/" + entry->guid;
        entry->location->length = entity.size();
        std::ofstream file(fixture.root / entry->location->path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(entity.data()), static_cast<std::streamsize>(entity.size()));
        file.close();
        if (!file)
        {
            return false;
        }
        fixture.catalog.entries.push_back(std::move(entry));
        return true;
    };
    constexpr auto silent_guid = "30000000-0000-4000-8000-000000000001";
    constexpr auto sound_guid = "30000000-0000-4000-8000-000000000002";
    constexpr auto movie_guid = "30000000-0000-4000-8000-000000000003";
    if (!add("movies/test.mp4", silent_guid, schema::AssetKind::movie, builder))
    {
        return false;
    }
    schema::AudioClipDataT sound;
    sound.sample_rate = 48000;
    sound.channels = 2;
    sound.frames = 14400;
    sound.samples.resize(28800, 0.05F);
    flatbuffers::FlatBufferBuilder audio_builder;
    schema::FinishAudioClipDataBuffer(audio_builder, schema::AudioClipData::Pack(audio_builder, &sound));
    if (!add("movies/test-audio.wav", sound_guid, schema::AssetKind::audio, audio_builder))
    {
        return false;
    }
    movie.audio_guid = sound_guid;
    flatbuffers::FlatBufferBuilder movie_builder;
    schema::FinishMovieDataBuffer(movie_builder, schema::MovieData::Pack(movie_builder, &movie));
    return add("movies/sound.mp4", movie_guid, schema::AssetKind::movie, movie_builder,
               {sound_guid});
}

bool run()
{
    if (!SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy") || !SDL_Init(SDL_INIT_AUDIO))
    {
        return expect(false, "initialize SDL test audio");
    }
    ThreadSystem threads({2});
    TaskSystem tasks(threads, {"MovieTest", 2});
    ResourceSystem resources;
    AudioSystem audio;
    test_support::ResourceFixture fixture;
    if (!threads.initialize({}) || !tasks.initialize({}) || !resources.initialize({}) ||
        !resources.configure_async(tasks) || !audio.initialize({}) || !make_movie(fixture) ||
        !fixture.publish(resources))
    {
        return expect(false, "initialize services and fixture");
    }
    MoviePlayer player(resources, tasks, audio);
    MoviePlaybackHandle handle;
    if (!expect(player.play("movies/test.mp4", handle), "accept asynchronous movie") ||
        !expect(!player.play("movies/test.mp4", handle), "reject busy player"))
    {
        return false;
    }
    if (!player.current_handle(handle))
    {
        return false;
    }
    MoviePlaybackStatus status;
    MovieFrame frame;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool first_frame = false;
    while (std::chrono::steady_clock::now() < deadline)
    {
        resources.poll_async();
        (void)tasks.dispatch_completions(16, 0.01);
        if (!player.update(0) || !player.status(handle, status) || status.state == MovieState::failed)
        {
            std::fprintf(stderr, "movie failure: %s\n", status.error.c_str());
            return false;
        }
        if (player.frame(handle, frame))
        {
            first_frame = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!expect(first_frame && frame.width == 8 && frame.height == 8 &&
                    frame.presentation_ticks == 0 && frame.planes[0].bytes.size() == 64,
                "first VP9 frame decoded asynchronously") ||
        !expect(player.set_paused(handle, true) && player.update(10) &&
                    player.status(handle, status) && status.position_seconds == 0,
                "paused clock stays still") ||
        !expect(player.set_paused(handle, false), "resume"))
    {
        return false;
    }
    const auto old_handle = handle;
    if (!expect(!player.stop(handle, static_cast<MovieStopReason>(99)) &&
                    player.status(handle, status) && status.state == MovieState::playing,
                "invalid stop reason leaves playback active") ||
        !expect(player.stop(handle, MovieStopReason::stopped) && player.release(handle) &&
                    player.play("movies/test.mp4", handle),
                "cancel an active decode and start a new request"))
    {
        return false;
    }
    while (std::chrono::steady_clock::now() < deadline)
    {
        resources.poll_async();
        (void)tasks.dispatch_completions(16, 0.01);
        if (!player.update(0.02) || !player.status(handle, status) || status.state == MovieState::failed)
        {
            return false;
        }
        if (status.state == MovieState::completed)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!expect(status.state == MovieState::completed && player.frame(handle, frame) &&
                    frame.presentation_ticks == 2,
                "complete after final frame") ||
        !expect(player.release(handle), "release completed movie"))
    {
        return false;
    }
    MoviePlaybackHandle next;
    if (!expect(player.play("movies/test.mp4", next) && !player.stop(old_handle, MovieStopReason::stopped) &&
                    player.stop(next, MovieStopReason::skipped) && player.release(next),
                "old handle cannot affect subsequent request") ||
        !expect(player.play("movies/sound.mp4", next), "accept movie with audio"))
    {
        return false;
    }
    bool saw_playing = false;
    bool paused_audio = false;
    const auto audio_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < audio_deadline)
    {
        resources.poll_async();
        (void)tasks.dispatch_completions(16, 0.01);
        if (!audio.tick(0.001) || !player.update(0.001) || !player.status(next, status) ||
            status.state == MovieState::failed)
        {
            std::fprintf(stderr, "audio movie failure: %s\n", status.error.c_str());
            return false;
        }
        saw_playing |= status.state == MovieState::playing;
        if (!paused_audio && status.state == MovieState::playing)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            if (!player.set_paused(next, true) || !player.status(next, status))
            {
                return false;
            }
            const auto paused_at = status.position_seconds;
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            if (!expect(player.update(1.0) && player.status(next, status) &&
                            status.state == MovieState::paused && status.position_seconds == paused_at,
                        "audio clock remains frozen while paused") ||
                !player.set_paused(next, false))
            {
                return false;
            }
            paused_audio = true;
        }
        if (status.state == MovieState::completed)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return expect(saw_playing && paused_audio && status.state == MovieState::completed &&
                      status.position_seconds == status.duration_seconds &&
                      player.frame(next, frame) && frame.presentation_ticks == 2 &&
                      player.release(next), "audio playback and video both finish");
}
} // namespace
int main()
{
    return run() ? 0 : 1;
}
