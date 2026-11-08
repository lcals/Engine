#pragma once

#include "resource_fixture.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/movie_generated.h"

#include <vpx/vp8cx.h>
#include <vpx/vpx_encoder.h>

#include <cstdio>
#include <fstream>
#include <vector>

namespace test_support
{
using engine::ByteBuffer;
using engine::ResourceSystem;
namespace schema = engine::schema;
inline void append_movie_le(std::vector<std::uint8_t>& bytes, std::uint64_t value,
                            unsigned int width)
{
    for (unsigned int index = 0; index < width; ++index)
    {
        bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8)));
    }
}
inline bool make_movie(ResourceFixture& fixture, std::uint32_t frame_count = 3,
                       std::uint32_t frame_rate = 10)
{
    vpx_codec_enc_cfg_t config{};
    if (vpx_codec_enc_config_default(vpx_codec_vp9_cx(), &config, 0) != VPX_CODEC_OK)
    {
        return false;
    }
    config.g_w = 8;
    config.g_h = 8;
    config.g_timebase = {1, static_cast<int>(frame_rate)};
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
    std::vector<std::uint8_t> ivf{'D', 'K', 'I', 'F', 0, 0, 32, 0, 'V', 'P', '9', '0'};
    append_movie_le(ivf, 8, 2);
    append_movie_le(ivf, 8, 2);
    append_movie_le(ivf, frame_rate, 4);
    append_movie_le(ivf, 1, 4);
    append_movie_le(ivf, frame_count, 4);
    append_movie_le(ivf, 0, 4);
    bool success = true;
    for (std::uint32_t frame = 0; frame < frame_count && success; ++frame)
    {
        for (int plane = 0; plane < 3; ++plane)
        {
            const int size = plane == 0 ? 8 : 4;
            for (int y = 0; y < size; ++y)
            {
                for (int x = 0; x < size; ++x)
                {
                    image.planes[plane][y * image.stride[plane] + x] =
                        static_cast<unsigned char>(plane == 0 ? 40 + frame % 4 * 50 : 128);
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
            append_movie_le(ivf, size, 4);
            append_movie_le(ivf, frame, 8);
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
    movie.video->frame_rate_num = frame_rate;
    movie.video->frame_rate_den = 1;
    movie.video->time_base_num = 1;
    movie.video->time_base_den = frame_rate;
    movie.video->frame_count = frame_count;
    movie.video->duration_ticks = frame_count;
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
        if (!ResourceSystem::encode_resource(
                entry->guid, entry->kind,
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
        file.write(reinterpret_cast<const char*>(entity.data()),
                   static_cast<std::streamsize>(entity.size()));
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
    sound.frames = std::uint64_t{frame_count} * sound.sample_rate / frame_rate;
    sound.samples.resize(static_cast<std::size_t>(sound.frames) * sound.channels, 0.05F);
    flatbuffers::FlatBufferBuilder audio_builder;
    schema::FinishAudioClipDataBuffer(audio_builder,
                                      schema::AudioClipData::Pack(audio_builder, &sound));
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

} // namespace test_support
