#include "runtime/resource/codecs/movie_resource_codec.hpp"

#include "runtime/resource/types/audio_resource.hpp"
#include "runtime/schema/movie_generated.h"
#include <set>

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

bool valid_video(const schema::MovieVideoDataT& video) noexcept
{
    const auto* color = video.color.get();
    const auto& payload = video.payload;
    if (payload.empty() || !color || video.width == 0 || video.height == 0 || video.width > 1920 ||
        video.height > 1080 || video.frame_rate_num == 0 || video.frame_rate_den == 0 ||
        std::uint64_t{video.frame_rate_num} > std::uint64_t{video.frame_rate_den} * 60 ||
        video.time_base_num != video.frame_rate_den ||
        video.time_base_den != video.frame_rate_num || video.frame_count == 0 ||
        video.duration_ticks != video.frame_count ||
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
    const auto bytes = std::span<const std::uint8_t>(payload.data(), payload.size());
    if (bytes.size() < 32 || std::memcmp(bytes.data(), "DKIF", 4) != 0 ||
        read16(bytes.data() + 4) != 0 || read16(bytes.data() + 6) != 32 ||
        std::memcmp(bytes.data() + 8, "VP90", 4) != 0 || read16(bytes.data() + 12) != video.width ||
        read16(bytes.data() + 14) != video.height ||
        read32(bytes.data() + 16) != video.frame_rate_num ||
        read32(bytes.data() + 20) != video.frame_rate_den ||
        read32(bytes.data() + 24) != video.frame_count)
    {
        return false;
    }
    std::size_t offset = 32;
    for (std::uint32_t frame = 0; frame < video.frame_count; ++frame)
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

schema::AssetKind MovieResourceCodec::kind() const noexcept
{
    return schema::AssetKind::movie;
}

bool MovieResourceCodec::decode(const schema::AssetEntryT& entry, ByteView payload,
                                std::unique_ptr<Resource>& output, std::string& error) const
{
    if (output)
    {
        error = "Resource output must be empty";
        return false;
    }
    error.clear();
    if (entry.kind != kind() || payload.empty() || payload.size() > 64U * 1024U * 1024U)
    {
        error = "Invalid resource kind or payload size";
        return false;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
    flatbuffers::Verifier verifier(bytes, payload.size());
    if (!schema::VerifyMovieDataBuffer(verifier))
    {
        error = "Invalid Movie FlatBuffer";
        return false;
    }
    auto data = schema::UnPackMovieData(bytes);
    output = std::make_unique<ResourceType>(entry, std::move(*data));
    return true;
}

bool MovieResourceCodec::validate_data(const Resource& resource, std::string& error) const
{
    error.clear();
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data || data->schema_version != 1 || data->encoding_version != 1 || !data->video ||
        !valid_video(*data->video))
    {
        error = "Invalid movie video or IVF stream";
        return false;
    }
    return data->audio_guid.empty()
               ? resource.info().dependencies.empty()
               : resource.info().dependencies.size() == 1 && declares(resource, data->audio_guid);
}

bool MovieResourceCodec::validate_dependencies(const Resource& resource, ResourceView dependencies,
                                               std::string& error) const
{
    error.clear();
    const auto* movie = resource_detail::data<ResourceType>(&resource);
    if (!movie || !movie->video)
    {
        error = "Invalid movie type";
        return false;
    }
    if (movie->audio_guid.empty())
    {
        return dependencies.empty();
    }
    const auto* clip =
        resource_detail::data<AudioResource>(dependency(dependencies, movie->audio_guid));
    if (dependencies.size() != 1 || !clip)
    {
        error = "Missing movie audio";
        return false;
    }
    const auto& video = *movie->video;
    const long double expected = static_cast<long double>(video.frame_count) *
                                 video.frame_rate_den * clip->sample_rate / video.frame_rate_num;
    if (std::abs(static_cast<long double>(clip->frames) - expected) > 1.0L)
    {
        error = "Movie audio duration mismatch";
        return false;
    }
    return true;
}
} // namespace engine
