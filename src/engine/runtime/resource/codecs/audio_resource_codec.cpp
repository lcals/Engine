#include "runtime/resource/codecs/audio_resource_codec.hpp"

#include <flatbuffers/verifier.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace engine
{
namespace
{
constexpr std::size_t maximum_samples = (64U * 1024U * 1024U - 128U * 1024U) / sizeof(float);
}

bool AudioResourceCodec::validate(const schema::AudioClipDataT& data) noexcept
{
    if (data.schema_version != 1 || data.sample_rate < 8000 || data.sample_rate > 192000 ||
        (data.channels != 1 && data.channels != 2) || data.frames == 0 ||
        data.samples.size() > maximum_samples || data.frames > maximum_samples / data.channels ||
        data.samples.size() != data.frames * data.channels)
    {
        return false;
    }
    return std::all_of(data.samples.begin(), data.samples.end(), [](float value)
                       { return std::isfinite(value) && value >= -1.0F && value <= 1.0F; });
}

bool AudioResourceCodec::decode(const ResourceCatalog&, ByteView payload, Resource& output) const
{
    if (payload.empty() || payload.size() > 64U * 1024U * 1024U ||
        !output.info().dependencies.empty())
    {
        return false;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
    flatbuffers::Verifier verifier(bytes, payload.size());
    if (!schema::VerifyAudioClipDataBuffer(verifier))
    {
        return false;
    }
    const auto* encoded = schema::GetAudioClipData(bytes);
    if (encoded->samples()->size() > maximum_samples)
    {
        return false;
    }
    auto data = schema::UnPackAudioClipData(bytes);
    if (!validate(*data))
    {
        return false;
    }
    store(output, std::move(data));
    return true;
}
} // namespace engine
