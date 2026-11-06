#include "runtime/resource/codecs/movie_resource_codec.hpp"

#include "runtime/schema/movie_generated.h"

#include <flatbuffers/verifier.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>

namespace engine
{
namespace
{
std::uint16_t read16(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint16_t>(p[0] | (std::uint16_t{p[1]} << 8));
}
std::uint32_t read32(const std::uint8_t* p) noexcept
{
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) |
           (std::uint32_t{p[3]} << 24);
}
std::uint64_t read64(const std::uint8_t* p) noexcept
{
    return read32(p) | (std::uint64_t{read32(p + 4)} << 32);
}

bool valid_video(const schema::MovieVideoData& video) noexcept
{
    const auto* color = video.color();
    const auto* payload = video.payload();
    if (!payload || !color || video.width() == 0 || video.height() == 0 || video.width() > 1920 ||
        video.height() > 1080 || video.frame_rate_num() == 0 || video.frame_rate_den() == 0 ||
        std::uint64_t{video.frame_rate_num()} > std::uint64_t{video.frame_rate_den()} * 60 ||
        video.time_base_num() != video.frame_rate_den() ||
        video.time_base_den() != video.frame_rate_num() || video.frame_count() == 0 ||
        video.duration_ticks() != video.frame_count() ||
        color->primaries() != schema::MovieColorPrimaries::bt709 ||
        (color->transfer() != schema::MovieTransfer::srgb &&
         color->transfer() != schema::MovieTransfer::bt709) ||
        (color->matrix() != schema::MovieColorMatrix::bt601 &&
         color->matrix() != schema::MovieColorMatrix::bt709) ||
        (color->range() != schema::MovieColorRange::full &&
         color->range() != schema::MovieColorRange::limited) ||
        (color->chroma_location() != schema::MovieChromaLocation::left &&
         color->chroma_location() != schema::MovieChromaLocation::center))
    {
        return false;
    }
    const auto bytes = std::span<const std::uint8_t>(payload->data(), payload->size());
    if (bytes.size() < 32 || std::memcmp(bytes.data(), "DKIF", 4) != 0 ||
        read16(bytes.data() + 4) != 0 || read16(bytes.data() + 6) != 32 ||
        std::memcmp(bytes.data() + 8, "VP90", 4) != 0 ||
        read16(bytes.data() + 12) != video.width() || read16(bytes.data() + 14) != video.height() ||
        read32(bytes.data() + 16) != video.frame_rate_num() ||
        read32(bytes.data() + 20) != video.frame_rate_den() ||
        read32(bytes.data() + 24) != video.frame_count())
    {
        return false;
    }
    std::size_t offset = 32;
    for (std::uint32_t frame = 0; frame < video.frame_count(); ++frame)
    {
        if (bytes.size() - offset < 12)
        {
            return false;
        }
        const auto size = read32(bytes.data() + offset);
        if (size == 0 || read64(bytes.data() + offset + 4) != frame)
        {
            return false;
        }
        offset += 12;
        if (size > bytes.size() - offset)
        {
            return false;
        }
        offset += size;
    }
    return offset == bytes.size();
}
} // namespace

bool MovieResourceCodec::decode(const ResourceCatalog& catalog, ByteView payload,
                                Resource& output) const
{
    if (payload.empty() || payload.size() > 64U * 1024U * 1024U)
    {
        return false;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
    flatbuffers::Verifier verifier(bytes, payload.size());
    if (!schema::VerifyMovieDataBuffer(verifier))
    {
        return false;
    }
    const auto* movie = schema::GetMovieData(bytes);
    if (movie->schema_version() != 1 || movie->encoding_version() != 1 || !movie->video() ||
        !valid_video(*movie->video()))
    {
        return false;
    }
    const auto* audio = movie->audio_guid();
    if ((!audio || audio->size() == 0)
            ? !output.info().dependencies.empty()
            : output.info().dependencies.size() != 1 ||
                  !declares(catalog, output, audio->string_view(), schema::AssetKind::audio))
    {
        return false;
    }
    store(output, payload);
    return true;
}

bool MovieResourceCodec::validate_dependencies(const ResourceCatalog&, const Resource& resource,
                                               std::span<const ResourcePtr> dependencies) const
{
    const auto bytes = resource.bytes();
    if (bytes.empty())
    {
        return false;
    }
    const auto* movie = schema::GetMovieData(bytes.data());
    const auto* guid = movie->audio_guid();
    if (!guid || guid->size() == 0)
    {
        return dependencies.empty();
    }
    const auto* audio = dependency(dependencies, guid->string_view());
    if (dependencies.size() != 1 || !audio || !audio->audio())
    {
        return false;
    }
    const auto& clip = *audio->audio();
    const auto& video = *movie->video();
    const long double expected = static_cast<long double>(video.frame_count()) *
                                 video.frame_rate_den() * clip.sample_rate / video.frame_rate_num();
    return std::abs(static_cast<long double>(clip.frames) - expected) <= 1.0L;
}
} // namespace engine
