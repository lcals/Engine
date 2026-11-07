#include "runtime/resource/codecs/audio_resource_codec.hpp"

#include <flatbuffers/verifier.h>
#include <set>

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

schema::AssetKind AudioResourceCodec::kind() const noexcept
{
    return schema::AssetKind::audio;
}

bool AudioResourceCodec::decode(const schema::AssetEntryT& entry, ByteView payload,
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
    if (!schema::VerifyAudioClipDataBuffer(verifier))
    {
        error = "Invalid Audio FlatBuffer";
        return false;
    }
    if (schema::GetAudioClipData(bytes)->samples()->size() > maximum_samples)
    {
        error = "Audio sample limit exceeded";
        return false;
    }
    auto data = schema::UnPackAudioClipData(bytes);
    output = std::make_unique<ResourceType>(entry, std::move(*data));
    return true;
}

bool AudioResourceCodec::validate_data(const Resource& resource, std::string& error) const
{
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data ||
        !(data->schema_version == 1 && resource.info().dependencies.empty() && validate(*data)))
    {
        error = "Invalid Audio data or declared dependencies";
        return false;
    }
    error.clear();
    return true;
}

bool AudioResourceCodec::validate_dependencies(const Resource& resource, ResourceView dependencies,
                                               std::string& error) const
{
    error.clear();
    return resource.info().dependencies.empty() && dependencies.empty();
}
} // namespace engine
