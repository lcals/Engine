#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/codecs/audio_resource_codec.hpp"
#include "runtime/resource/resource_system.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
using namespace engine;
namespace fs = std::filesystem;

bool expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::fprintf(stderr, "audio pipeline: %.*s\n", static_cast<int>(message.size()),
                     message.data());
    }
    return condition;
}

bool write(const fs::path& path, std::string_view text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    return static_cast<bool>(stream);
}

std::string read(const fs::path& path)
{
    std::string text;
    return ResourceSystem::read_text(path, text) ? text : std::string{};
}

void append_u16(std::string& text, std::uint16_t value)
{
    text.push_back(static_cast<char>(value & 255U));
    text.push_back(static_cast<char>(value >> 8));
}

void append_u32(std::string& text, std::uint32_t value)
{
    for (unsigned int index = 0; index < 4; ++index)
    {
        text.push_back(static_cast<char>((value >> (index * 8)) & 255U));
    }
}

std::string wav(std::uint16_t channels, std::uint32_t rate, std::uint16_t encoding,
                std::uint16_t bits, std::string_view samples)
{
    std::string bytes = "RIFF";
    append_u32(bytes, static_cast<std::uint32_t>(36 + samples.size() + (samples.size() & 1U)));
    bytes += "WAVEfmt ";
    append_u32(bytes, 16);
    append_u16(bytes, encoding);
    append_u16(bytes, channels);
    append_u32(bytes, rate);
    append_u32(bytes, rate * channels * (bits / 8));
    append_u16(bytes, channels * (bits / 8));
    append_u16(bytes, bits);
    bytes += "data";
    append_u32(bytes, static_cast<std::uint32_t>(samples.size()));
    bytes += samples;
    if ((samples.size() & 1U) != 0)
    {
        bytes.push_back('\0');
    }
    return bytes;
}

std::string metadata(std::string_view guid, std::string_view settings)
{
    return "guid = \"" + std::string(guid) +
           "\"\nimporter = \"audio\"\nmeta_version = 1\n[audio]\n" + std::string(settings);
}

struct Project
{
    fs::path root;

    bool initialize()
    {
        std::error_code error;
        root = fs::temp_directory_path(error);
        if (error)
        {
            return false;
        }
        root /= "engine-audio-pipeline-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        for (const char* directory : {"assets", "settings", "cache"})
        {
            fs::create_directories(root / directory, error);
            if (error)
            {
                return false;
            }
        }
        return true;
    }

    ~Project()
    {
        std::error_code error;
        if (!root.empty())
        {
            fs::remove_all(root, error);
        }
    }
};
} // namespace

int main()
{
    Project project;
    if (!expect(project.initialize(), "create project"))
    {
        return 1;
    }
    std::string samples;
    for (std::size_t frame = 0; frame < 480; ++frame)
    {
        append_u16(samples, 16384);
    }
    const auto source = wav(1, 48000, 1, 16, samples);
    const auto path = project.root / "assets/tone.WAV";
    const auto meta = project.root / "assets/tone.WAV.meta";
    editor::AssetPipelineSystem pipeline;
    ResourceSystem resources;
    if (!expect(write(path, source), "write PCM WAV") ||
        !expect(pipeline.initialize({}), "initialize pipeline") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 1, "first build imports the WAV") ||
        !expect(resources.initialize({}), "initialize resource core") ||
        !expect(resources.update_catalog(pipeline.catalog(), {{"content", project.root / "cache"}}),
                resources.last_error()))
    {
        return 1;
    }
    Guid guid;
    ResourcePtr original;
    if (!expect(resources.catalog().identities().path_to_guid("project/tone.WAV", guid),
                "audio has a stable GUID") ||
        !expect(read(meta).find("[audio]") != std::string::npos &&
                    read(meta).find("sample_rate = 48000") != std::string::npos,
                "new audio receives default adjacent metadata") ||
        !expect(resources.load(guid, original), resources.last_error()) ||
        !expect(original->info().kind == schema::AssetKind::audio && original->audio() != nullptr,
                "core decodes AudioClipData") ||
        !expect(original->audio()->sample_rate == 48000 && original->audio()->channels == 2 &&
                    original->audio()->frames == 480 && original->audio()->samples.size() == 960,
                "default import converts mono PCM16 into stereo float") ||
        !expect(std::abs(original->audio()->samples[100] - 0.5F) < 0.001F,
                "decoded waveform is preserved") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 0 && pipeline.last_build().reused == 1,
                "unchanged audio reuses cached entity"))
    {
        return 1;
    }
    const auto old_revision = pipeline.last_build().revision;
    const auto changed_meta = metadata(guid, "sample_rate = 24000\nchannels = 1\ngain = 0.5\n");
    ResourcePtr changed;
    if (!expect(write(meta, changed_meta), "update audio import settings") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 1 &&
                    pipeline.last_build().revision != old_revision,
                "audio metadata change regenerates its GUID cache") ||
        !expect(resources.update_catalog(pipeline.catalog(), {{"content", project.root / "cache"}}),
                resources.last_error()) ||
        !expect(resources.load(guid, changed), resources.last_error()) ||
        !expect(changed->audio()->sample_rate == 24000 && changed->audio()->channels == 1 &&
                    changed->audio()->frames == 240 &&
                    std::abs(changed->audio()->samples[100] - 0.25F) < 0.01F,
                "resampling, channel conversion and gain affect generated PCM") ||
        !expect(original->audio()->channels == 2 && original->audio()->sample_rate == 48000,
                "old resource reference remains unchanged"))
    {
        return 1;
    }
    const auto revision = pipeline.last_build().revision;
    const auto published = read(project.root / "cache/gen" / guid);
    const auto rejects = [&](std::string_view data, std::string_view meta_text, const char* label)
    {
        return expect(write(path, data) && write(meta, meta_text),
                      "write invalid import fixture") &&
               expect(!pipeline.build_assets(project.root), label) &&
               expect(pipeline.last_build().revision == revision &&
                          read(project.root / "cache/gen" / guid) == published,
                      "failed audio import preserves the published version");
    };
    if (!rejects(source.substr(0, source.size() - 1), changed_meta, "reject truncated WAV") ||
        !rejects(wav(9, 48000, 1, 16, std::string(18, '\0')), changed_meta,
                 "reject unsupported source channel count") ||
        !rejects(wav(1, 48000, 17, 16, samples), changed_meta, "reject compressed WAV"))
    {
        return 1;
    }
    for (const auto* invalid : {"sample_rate = \"48000\"\n", "sample_rate = 1\n", "channels = 3\n",
                                "gain = nan\n", "gain = 5\n", "compression = \"ogg\"\n"})
    {
        if (!rejects(source, metadata(guid, invalid), "reject invalid audio metadata"))
        {
            return 1;
        }
    }
    std::string nonfinite;
    append_u32(nonfinite, std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()));
    if (!rejects(wav(1, 48000, 3, 32, nonfinite), changed_meta, "reject NaN WAV samples") ||
        !rejects(wav(1, 8000, 1, 8, std::string(400000, static_cast<char>(128))),
                 metadata(guid, "sample_rate = 192000\nchannels = 2\n"),
                 "reject oversized resampling before allocation"))
    {
        return 1;
    }
    auto invalid_clip = *changed->audio();
    invalid_clip.frames = std::numeric_limits<std::uint64_t>::max();
    if (!expect(!AudioResourceCodec::validate(invalid_clip), "core rejects overflowing frames"))
    {
        return 1;
    }
    invalid_clip = *changed->audio();
    invalid_clip.samples[0] = std::numeric_limits<float>::infinity();
    if (!expect(!AudioResourceCodec::validate(invalid_clip), "core rejects nonfinite PCM"))
    {
        return 1;
    }
    invalid_clip.samples[0] = 1.01F;
    if (!expect(!AudioResourceCodec::validate(invalid_clip), "core rejects unnormalized PCM"))
    {
        return 1;
    }
    changed.reset();
    original.reset();
    resources.shutdown();
    pipeline.shutdown();
    return 0;
}
