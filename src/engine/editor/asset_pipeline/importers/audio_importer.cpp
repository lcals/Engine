#include "editor/asset_pipeline/importers/audio_importer.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "runtime/resource/codecs/audio_resource_codec.hpp"
#include "runtime/resource/resource_system.hpp"

#include <SDL3/SDL.h>
#include <flatbuffers/flatbuffers.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::editor::asset_pipeline
{
namespace
{
constexpr std::uint64_t maximum_read_size = 64U * 1024U * 1024U;
constexpr std::uint64_t maximum_pcm_size = maximum_read_size - 128U * 1024U;

struct AudioSettings
{
    std::uint32_t sample_rate = 48000;
    std::uint32_t channels = 2;
    double gain = 1.0;
};

struct WavInfo
{
    std::uint32_t sample_rate = 0;
    std::uint32_t channels = 0;
    std::uint64_t frames = 0;
};

std::uint32_t little_u32(ByteView bytes, std::size_t offset) noexcept
{
    std::uint32_t value = 0;
    for (unsigned int index = 0; index < 4; ++index)
    {
        value |= std::to_integer<std::uint32_t>(bytes[offset + index]) << (index * 8);
    }
    return value;
}

std::uint16_t little_u16(ByteView bytes, std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(std::to_integer<unsigned int>(bytes[offset]) |
                                      (std::to_integer<unsigned int>(bytes[offset + 1]) << 8));
}

bool fourcc(ByteView bytes, std::size_t offset, const char* value) noexcept
{
    return std::memcmp(bytes.data() + offset, value, 4) == 0;
}

bool inspect_wav(ByteView bytes, WavInfo& output, std::string& error)
{
    output = {};
    if (bytes.size() < 44 || bytes.size() > maximum_read_size || !fourcc(bytes, 0, "RIFF") ||
        !fourcc(bytes, 8, "WAVE") || little_u32(bytes, 4) != bytes.size() - 8)
    {
        error = "audio source requires a complete bounded RIFF/WAVE file";
        return false;
    }
    ByteView format;
    ByteView samples;
    for (std::size_t offset = 12; offset < bytes.size();)
    {
        if (bytes.size() - offset < 8)
        {
            error = "WAV has an incomplete chunk header";
            return false;
        }
        const auto size = little_u32(bytes, offset + 4);
        const std::uint64_t padded_size = static_cast<std::uint64_t>(size) + (size & 1U);
        if (padded_size > bytes.size() - offset - 8)
        {
            error = "WAV chunk exceeds the source length";
            return false;
        }
        if (fourcc(bytes, offset, "fmt "))
        {
            if (!format.empty() || size < 16)
            {
                error = "WAV must have one complete format chunk";
                return false;
            }
            format = bytes.subspan(offset + 8, size);
        }
        else if (fourcc(bytes, offset, "data"))
        {
            if (!samples.empty() || size == 0)
            {
                error = "WAV must have one nonempty data chunk";
                return false;
            }
            samples = bytes.subspan(offset + 8, size);
        }
        offset += 8 + static_cast<std::size_t>(padded_size);
    }
    if (format.empty() || samples.empty())
    {
        error = "WAV is missing format or sample data";
        return false;
    }
    if (format.size() == 17 || (format.size() >= 18 && little_u16(format, 16) > format.size() - 18))
    {
        error = "WAV format extension exceeds its chunk length";
        return false;
    }
    auto encoding = little_u16(format, 0);
    const auto channels = little_u16(format, 2);
    const auto rate = little_u32(format, 4);
    const auto alignment = little_u16(format, 12);
    const auto bits = little_u16(format, 14);
    if (encoding == 0xfffe)
    {
        constexpr std::array<std::uint8_t, 12> guid_tail = {0, 0,    0x10, 0,    0x80, 0,
                                                            0, 0xaa, 0,    0x38, 0x9b, 0x71};
        if (format.size() < 40 || little_u16(format, 16) < 22 || little_u16(format, 18) != bits ||
            little_u16(format, 26) != 0 ||
            std::memcmp(format.data() + 28, guid_tail.data(), guid_tail.size()) != 0)
        {
            error = "WAV extensible format requires a complete PCM or float subtype";
            return false;
        }
        encoding = little_u16(format, 24);
    }
    const bool supported =
        (encoding == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) ||
        (encoding == 3 && bits == 32);
    if (!supported || channels == 0 || channels > 8 || rate < 8000 || rate > 192000 ||
        alignment != channels * (bits / 8) || alignment == 0 ||
        little_u32(format, 8) != static_cast<std::uint64_t>(rate) * alignment ||
        samples.size() % alignment != 0)
    {
        error = "WAV requires aligned PCM/float samples, 1..8 channels and 8000..192000 Hz";
        return false;
    }
    const auto frames = samples.size() / alignment;
    if (frames > maximum_read_size / (channels * sizeof(float)))
    {
        error = "decoded WAV exceeds the audio size limit";
        return false;
    }
    if (encoding == 3)
    {
        for (std::size_t offset = 0; offset < samples.size(); offset += sizeof(float))
        {
            if (!std::isfinite(std::bit_cast<float>(little_u32(samples, offset))))
            {
                error = "WAV float samples must be finite";
                return false;
            }
        }
    }
    output = {rate, channels, frames};
    return true;
}

bool read_settings(const std::filesystem::path& source, AudioSettings& output, std::string& error)
{
    auto meta = source;
    meta += ".meta";
    toml::table root;
    if (!read_table(meta, root, error))
    {
        return false;
    }
    if (!root.contains("audio"))
    {
        return true;
    }
    const auto* table = root["audio"].as_table();
    if (table == nullptr)
    {
        error = "audio meta requires an [audio] table";
        return false;
    }
    for (const auto& [key, value] : *table)
    {
        static_cast<void>(value);
        if (key != "sample_rate" && key != "channels" && key != "gain")
        {
            error = "unknown audio meta setting: " + std::string(key.str());
            return false;
        }
    }
    for (const char* name : {"sample_rate", "channels"})
    {
        if (!table->contains(name))
        {
            continue;
        }
        const auto value = (*table)[name].value<std::int64_t>();
        const bool rate = std::string_view(name) == "sample_rate";
        if (!(*table)[name].is_integer() || !value || *value < (rate ? 8000 : 1) ||
            *value > (rate ? 192000 : 2))
        {
            error = "audio setting is out of range: " + std::string(name);
            return false;
        }
        (rate ? output.sample_rate : output.channels) = static_cast<std::uint32_t>(*value);
    }
    if (table->contains("gain"))
    {
        const auto value = (*table)["gain"].value<double>();
        if ((!(*table)["gain"].is_integer() && !(*table)["gain"].is_floating_point()) || !value ||
            !std::isfinite(*value) || *value < 0.0 || *value > 4.0)
        {
            error = "audio gain must be a finite number in [0,4]";
            return false;
        }
        output.gain = *value;
    }
    return true;
}
} // namespace

schema::AssetKind AudioImporter::kind() const noexcept
{
    return schema::AssetKind::audio;
}

bool AudioImporter::matches(const std::filesystem::path& source) const
{
    return source_extension(source) == ".wav";
}

std::string_view AudioImporter::default_meta() const noexcept
{
    return "\n[audio]\nsample_rate = 48000\nchannels = 2\ngain = 1.0\n";
}

bool AudioImporter::inspect_batch(const ImportContext&, std::string& fingerprint,
                                  std::string& error)
{
    if (!schema_fingerprint({"audio.fbs"}, fingerprint, error))
    {
        return false;
    }
    fingerprint +=
        ":audio-importer-1:sdl:" + std::to_string(SDL_GetVersion()) + ":" + SDL_GetRevision();
    return true;
}

bool AudioImporter::convert(const ImportAsset& asset, const ImportContext& context,
                            std::string& error) const
{
    AudioSettings settings;
    ByteBuffer source;
    WavInfo info;
    if (!read_settings(asset.source, settings, error) || !read_bytes(asset.source, source, error) ||
        !inspect_wav(source, info, error))
    {
        return false;
    }
    const auto expected_frames =
        (info.frames * settings.sample_rate + info.sample_rate - 1) / info.sample_rate;
    // 给重采样器留出有限滤波尾部，转换前约束其一次性分配。
    if ((expected_frames + 512) * settings.channels * sizeof(float) > maximum_pcm_size)
    {
        error = "resampled audio exceeds the PCM size limit";
        return false;
    }
    SDL_IOStream* stream = SDL_IOFromConstMem(source.data(), source.size());
    if (stream == nullptr)
    {
        error = "cannot open WAV memory stream: " + std::string(SDL_GetError());
        return false;
    }
    SDL_AudioSpec source_spec{};
    Uint8* decoded_bytes = nullptr;
    Uint32 decoded_size = 0;
    const bool loaded = SDL_LoadWAV_IO(stream, true, &source_spec, &decoded_bytes, &decoded_size);
    const std::unique_ptr<Uint8, decltype(&SDL_free)> decoded(decoded_bytes, SDL_free);
    const auto source_frame_size = SDL_AUDIO_FRAMESIZE(source_spec);
    if (!loaded || !decoded || source_spec.freq != static_cast<int>(info.sample_rate) ||
        source_spec.channels != static_cast<int>(info.channels) || source_frame_size <= 0 ||
        decoded_size == 0 || decoded_size > maximum_read_size ||
        decoded_size != info.frames * source_frame_size)
    {
        error =
            "WAV decoding failed or produced inconsistent frames: " + std::string(SDL_GetError());
        return false;
    }
    SDL_AudioSpec target{};
    target.freq = static_cast<int>(settings.sample_rate);
    target.channels = static_cast<int>(settings.channels);
    target.format = SDL_AUDIO_F32;
    Uint8* converted_bytes = nullptr;
    int converted_size = 0;
    const bool converted_ok =
        SDL_ConvertAudioSamples(&source_spec, decoded.get(), static_cast<int>(decoded_size),
                                &target, &converted_bytes, &converted_size);
    const std::unique_ptr<Uint8, decltype(&SDL_free)> converted(converted_bytes, SDL_free);
    const auto target_frame_size = settings.channels * sizeof(float);
    if (!converted_ok || !converted || converted_size <= 0 ||
        static_cast<std::uint64_t>(converted_size) > maximum_pcm_size ||
        static_cast<std::uint64_t>(converted_size) % target_frame_size != 0)
    {
        error = "WAV sample conversion failed or exceeded limits: " + std::string(SDL_GetError());
        return false;
    }
    const auto* samples = reinterpret_cast<const float*>(converted.get());
    schema::AudioClipDataT clip;
    clip.sample_rate = settings.sample_rate;
    clip.channels = static_cast<std::uint8_t>(settings.channels);
    clip.frames = static_cast<std::uint64_t>(converted_size) / target_frame_size;
    clip.samples.resize(static_cast<std::size_t>(converted_size) / sizeof(float));
    for (std::size_t index = 0; index < clip.samples.size(); ++index)
    {
        if (!std::isfinite(samples[index]))
        {
            error = "converted audio samples must be finite";
            return false;
        }
        clip.samples[index] =
            static_cast<float>(std::clamp(samples[index] * settings.gain, -1.0, 1.0));
    }
    if (!AudioResourceCodec::validate(clip))
    {
        error = "converted audio failed runtime format validation";
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishAudioClipDataBuffer(builder, schema::AudioClipData::Pack(builder, &clip));
    if (builder.GetSize() > maximum_read_size - 65536)
    {
        error = "compiled audio exceeds the resource payload size limit";
        return false;
    }
    return write_bytes(context.output / asset.guid,
                       std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                       error);
}
} // namespace engine::editor::asset_pipeline
