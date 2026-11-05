#include "plugins/editor/asset_import/movie_importer.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/import_tool.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "runtime/resource/codecs/audio_resource_codec.hpp"
#include "runtime/schema/audio_generated.h"
#include "runtime/schema/movie_generated.h"
#include "runtime/utils/path_utils.hpp"

#include <flatbuffers/flatbuffers.h>
#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <span>
#include <system_error>

namespace engine::plugins::asset_import
{
namespace
{
using namespace editor::asset_pipeline;
constexpr std::size_t max_size = 64U * 1024U * 1024U;
constexpr std::uint32_t frame_rate = 30;
struct Settings
{
    bool audio = true;
    std::uint32_t sample_rate = 48000;
    std::uint32_t channels = 2;
    double gain = 1.0;
};
struct Probe
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t frames = 0;
    bool audio = false;
};
bool settings_for(const std::filesystem::path& source, Settings& out, std::string& error)
{
    auto path = source;
    path += ".meta";
    toml::table table;
    if (!read_table(path, table, error))
    {
        return false;
    }
    if (const auto* movie = table["movie"].as_table())
    {
        for (const auto& [key, value] : *movie)
        {
            if (key != "audio" || !value.is_boolean())
            {
                error = "movie meta only supports boolean audio";
                return false;
            }
        }
        out.audio = (*movie)["audio"].value_or(true);
    }
    else if (table.contains("movie"))
    {
        error = "movie settings must be a table";
        return false;
    }
    if (const auto* audio = table["audio"].as_table())
    {
        for (const auto& [key, value] : *audio)
        {
            if (key != "sample_rate" && key != "channels" && key != "gain")
            {
                error = "unknown movie audio setting";
                return false;
            }
            (void)value;
        }
        auto rate = (*audio)["sample_rate"].value_or<std::int64_t>(48000);
        auto channels = (*audio)["channels"].value_or<std::int64_t>(2);
        auto gain = (*audio)["gain"].value_or<double>(1.0);
        if (rate < 8000 || rate > 192000 || channels < 1 || channels > 2 || !std::isfinite(gain) ||
            gain < 0 || gain > 4 ||
            (audio->contains("sample_rate") && !(*audio)["sample_rate"].is_integer()) ||
            (audio->contains("channels") && !(*audio)["channels"].is_integer()) ||
            (audio->contains("gain") && !(*audio)["gain"].is_floating_point() &&
             !(*audio)["gain"].is_integer()))
        {
            error = "invalid movie audio settings";
            return false;
        }
        out.sample_rate = static_cast<std::uint32_t>(rate);
        out.channels = static_cast<std::uint32_t>(channels);
        out.gain = gain;
    }
    else if (table.contains("audio"))
    {
        error = "audio settings must be a table";
        return false;
    }
    return true;
}
bool run(const std::vector<std::string>& args, std::string& output, std::string& error,
         unsigned seconds = 120, const CancellationCheck& cancel_requested = {})
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    const auto result = run_import_tool(
        args,
        [&]
        {
            return (cancel_requested && cancel_requested()) ||
                   std::chrono::steady_clock::now() >= deadline;
        },
        1024U * 1024U, output, error);
    if (result == ImportToolStatus::cancelled)
    {
        error = cancel_requested && cancel_requested() ? "movie import cancelled"
                                                       : "movie tool exceeded its time limit";
    }
    if (result != ImportToolStatus::success && !output.empty())
    {
        error += ": " + output;
    }
    return result == ImportToolStatus::success;
}
bool probe(const std::filesystem::path& source, Probe& info, std::string& error,
           const CancellationCheck& cancel_requested = {})
{
    info = {};
    std::filesystem::path executable;
    std::string output;
    if (!find_import_tool("ffprobe", executable, error) ||
        !run({PathUtils::to_utf8(executable), "-v", "error", "-show_streams", "-show_format", "-of",
              "json", PathUtils::to_utf8(source)},
             output, error, 15, cancel_requested))
    {
        return false;
    }
    auto json = nlohmann::json::parse(output, nullptr, false);
    if (json.is_discarded() || !json.is_object() || !json.contains("streams") ||
        !json["streams"].is_array())
    {
        error = "invalid ffprobe stream data";
        return false;
    }
    bool video = false;
    for (const auto& stream : json["streams"])
    {
        if (!stream.is_object() || !stream.contains("codec_type") ||
            !stream["codec_type"].is_string())
        {
            continue;
        }
        const auto type = stream["codec_type"].get<std::string>();
        if (type == "audio")
        {
            info.audio = true;
        }
        if (type != "video" || video)
        {
            continue;
        }
        if (!stream.contains("width") || !stream["width"].is_number_unsigned() ||
            !stream.contains("height") || !stream["height"].is_number_unsigned())
        {
            error = "movie video dimensions are missing";
            return false;
        }
        info.width = stream["width"].get<std::uint32_t>();
        info.height = stream["height"].get<std::uint32_t>();
        if (info.width < 2 || info.width > 1920 || info.height < 2 || info.height > 1920 ||
            info.width % 2 || info.height % 2)
        {
            error = "movie dimensions must be even and at most 1920x1080";
            return false;
        }
        if (stream.contains("side_data_list") && stream["side_data_list"].is_array())
        {
            for (const auto& side : stream["side_data_list"])
            {
                if (!side.is_object() || !side.contains("rotation"))
                {
                    continue;
                }
                if (!side["rotation"].is_number_integer())
                {
                    error = "movie rotation is invalid";
                    return false;
                }
                const int rotation = side["rotation"].get<int>();
                if (rotation == 90 || rotation == -90 || rotation == 270 || rotation == -270)
                {
                    std::swap(info.width, info.height);
                }
                else if (rotation != 0 && rotation != 180 && rotation != -180)
                {
                    error = "movie rotation is not a right angle";
                    return false;
                }
            }
        }
        if (info.width > 1920 || info.height > 1080)
        {
            error = "rotated movie dimensions exceed 1920x1080";
            return false;
        }
        for (const char* key : {"color_primaries", "color_transfer", "color_space"})
        {
            if (stream.contains(key) && stream[key].is_string())
            {
                const auto color = stream[key].get<std::string>();
                if (color != "bt709" && color != "unknown" && color != "unspecified")
                {
                    error = std::string("unsupported movie color metadata: ") + key + "=" + color;
                    return false;
                }
            }
        }
        video = true;
    }
    if (!video || !json.contains("format") || !json["format"].is_object() ||
        !json["format"].contains("duration") || !json["format"]["duration"].is_string())
    {
        error = "movie requires a video stream and a known duration";
        return false;
    }
    const auto duration = json["format"]["duration"].get<std::string>();
    const auto point = duration.find('.');
    const auto whole = duration.substr(0, point);
    const auto fraction = point == std::string::npos ? std::string{} : duration.substr(point + 1);
    std::uint32_t integer = 0;
    const auto parsed = std::from_chars(whole.data(), whole.data() + whole.size(), integer);
    if (whole.empty() || parsed.ec != std::errc{} || parsed.ptr != whole.data() + whole.size() ||
        fraction.size() > 9 || (point != std::string::npos && fraction.empty()) ||
        fraction.find_first_not_of("0123456789") != std::string::npos)
    {
        error = "invalid movie duration";
        return false;
    }
    double seconds = integer;
    double unit = 0.1;
    for (const char digit : fraction)
    {
        seconds += (digit - '0') * unit;
        unit *= 0.1;
    }
    if (seconds <= 0 || seconds > 120)
    {
        error = "movie duration must be greater than zero and at most 120 seconds";
        return false;
    }
    info.frames = static_cast<std::uint32_t>(std::max(1.0, std::round(seconds * frame_rate)));
    return true;
}
bool audio_identity(const ImportAsset& asset, const ImportContext& context, Guid& output,
                    std::string& error)
{
    output.clear();
    if (!context.identities.path_to_guid(asset.path + "/audio", output))
    {
        error = "movie audio subresource identity is missing";
        return false;
    }
    const auto kind = context.kinds.find(output);
    if (kind == context.kinds.end() || kind->second != schema::AssetKind::audio)
    {
        error = "movie audio subresource has invalid kind";
        return false;
    }
    return true;
}
std::uint32_t read32(const std::byte* p)
{
    return std::to_integer<std::uint32_t>(p[0]) | (std::to_integer<std::uint32_t>(p[1]) << 8) |
           (std::to_integer<std::uint32_t>(p[2]) << 16) |
           (std::to_integer<std::uint32_t>(p[3]) << 24);
}
} // namespace

std::string_view MovieImporter::id() const noexcept
{
    return "movie";
}
schema::AssetKind MovieImporter::kind() const noexcept
{
    return schema::AssetKind::movie;
}
bool MovieImporter::matches(const std::filesystem::path& source) const
{
    const auto ext = source_extension(source);
    return ext == ".mp4" || ext == ".mov";
}
std::string_view MovieImporter::default_meta() const noexcept
{
    return "\n[movie]\naudio = true\n\n[audio]\nsample_rate = 48000\nchannels = 2\ngain = 1.0\n";
}
bool MovieImporter::discover(const ImportAsset& source, std::vector<ImportSubresource>& output,
                             std::string& error) const
{
    output.clear();
    error.clear();
    Settings settings;
    Probe info;
    if (!settings_for(source.source, settings, error) || !probe(source.source, info, error))
    {
        return false;
    }
    if (settings.audio && info.audio)
    {
        output.push_back({"audio", schema::AssetKind::audio});
    }
    return true;
}
bool MovieImporter::inspect_batch(const ImportContext& context, std::string& fingerprint,
                                  std::string& error)
{
    fingerprint.clear();
    error.clear();
    std::filesystem::path ffmpeg;
    std::filesystem::path ffprobe;
    std::string video_version, probe_version, video_hash, probe_hash;
    if (!find_import_tool("ffmpeg", ffmpeg, error) ||
        !find_import_tool("ffprobe", ffprobe, error) ||
        !run({PathUtils::to_utf8(ffmpeg), "-version"}, video_version, error, 15,
             context.cancel_requested) ||
        !run({PathUtils::to_utf8(ffprobe), "-version"}, probe_version, error, 15,
             context.cancel_requested) ||
        !file_hash(ffmpeg, video_hash, error) || !file_hash(ffprobe, probe_hash, error) ||
        !schema_fingerprint({"movie.fbs", "audio.fbs"}, fingerprint, error))
    {
        return false;
    }
    fingerprint +=
        ":movie-1:vp9-ivf-30fps:" + video_hash + ":" + probe_hash + video_version + probe_version;
    return true;
}
bool MovieImporter::scan_dependencies(ImportAsset& asset, const ImportContext& context,
                                      std::string& error) const
{
    error.clear();
    asset.build_dependencies.clear();
    asset.runtime_dependencies.clear();
    if (!asset.subresource_key.empty())
    {
        return true;
    }
    Settings settings;
    Probe info;
    if (!settings_for(asset.source, settings, error) ||
        !probe(asset.source, info, error, context.cancel_requested))
    {
        return false;
    }
    if (settings.audio && info.audio)
    {
        Guid guid;
        if (!audio_identity(asset, context, guid, error))
        {
            return false;
        }
        asset.runtime_dependencies.push_back(guid);
    }
    return true;
}
bool MovieImporter::convert(const ImportAsset& asset, const ImportContext& context,
                            std::string& error) const
{
    error.clear();
    Settings settings;
    Probe info;
    std::filesystem::path executable;
    if (!settings_for(asset.source, settings, error) ||
        !probe(asset.source, info, error, context.cancel_requested) ||
        !find_import_tool("ffmpeg", executable, error))
    {
        return false;
    }
    const bool audio = asset.subresource_key == "audio";
    if ((!asset.subresource_key.empty() && !audio) || (audio && (!info.audio || !settings.audio)))
    {
        error = "movie subresource does not match source";
        return false;
    }
    const auto temp = context.output / (asset.guid + (audio ? ".movie-pcm" : ".movie-ivf"));
    std::error_code code;
    std::filesystem::remove(temp, code);
    std::vector<std::string> args = {PathUtils::to_utf8(executable),
                                     "-nostdin",
                                     "-hide_banner",
                                     "-loglevel",
                                     "error",
                                     "-y",
                                     "-filter_threads",
                                     "1",
                                     "-threads",
                                     "1",
                                     "-i",
                                     PathUtils::to_utf8(asset.source)};
    const auto samples =
        static_cast<std::uint64_t>(info.frames) * settings.sample_rate / frame_rate;
    if (audio)
    {
        if (samples * settings.channels * sizeof(float) > max_size - 131072)
        {
            error = "movie audio exceeds the resource size limit";
            return false;
        }
        args.insert(
            args.end(),
            {"-map", "0:a:0", "-vn", "-af",
             "aresample=async=1:first_pts=0,apad,atrim=end_sample=" + std::to_string(samples),
             "-ar", std::to_string(settings.sample_rate), "-ac", std::to_string(settings.channels),
             "-c:a", "pcm_f32le", "-f", "f32le"});
    }
    else
    {
        args.insert(args.end(),
                    {"-map",
                     "0:v:0",
                     "-an",
                     "-vf",
                     "fps=30:start_time=0,tpad=stop_mode=clone:stop_duration=1,scale="
                     "in_color_matrix=bt709:out_color_matrix=bt709:out_range=tv,format=yuv420p",
                     "-frames:v",
                     std::to_string(info.frames),
                     "-c:v",
                     "libvpx-vp9",
                     "-profile:v",
                     "0",
                     "-lag-in-frames",
                     "0",
                     "-auto-alt-ref",
                     "0",
                     "-threads",
                     "1",
                     "-deadline",
                     "good",
                     "-cpu-used",
                     "3",
                     "-crf",
                     "32",
                     "-b:v",
                     "0",
                     "-color_primaries",
                     "bt709",
                     "-color_trc",
                     "bt709",
                     "-colorspace",
                     "bt709",
                     "-color_range",
                     "tv",
                     "-f",
                     "ivf"});
    }
    args.push_back("-fs");
    args.push_back(std::to_string(max_size - 131072));
    args.push_back(PathUtils::to_utf8(temp));
    std::string diagnostic;
    if (!run(args, diagnostic, error, 120, context.cancel_requested))
    {
        std::filesystem::remove(temp, code);
        return false;
    }
    std::vector<std::byte> bytes;
    const bool loaded = read_bytes(temp, bytes, error);
    std::filesystem::remove(temp, code);
    if (!loaded || bytes.empty() || bytes.size() > max_size - 131072)
    {
        if (error.empty())
        {
            error = "movie converted payload exceeds size limit or is empty";
        }
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    if (audio)
    {
        if (bytes.size() != samples * settings.channels * sizeof(float))
        {
            error = "movie audio sample count does not match video";
            return false;
        }
        schema::AudioClipDataT clip;
        clip.sample_rate = settings.sample_rate;
        clip.channels = static_cast<std::uint8_t>(settings.channels);
        clip.frames = samples;
        clip.samples.resize(bytes.size() / sizeof(float));
        std::memcpy(clip.samples.data(), bytes.data(), bytes.size());
        for (float& value : clip.samples)
        {
            if (!std::isfinite(value))
            {
                error = "movie audio contains non-finite samples";
                return false;
            }
            value = static_cast<float>(std::clamp(value * settings.gain, -1.0, 1.0));
        }
        if (!AudioResourceCodec::validate(clip))
        {
            error = "movie audio failed validation";
            return false;
        }
        schema::FinishAudioClipDataBuffer(builder, schema::AudioClipData::Pack(builder, &clip));
    }
    else
    {
        if (bytes.size() < 32 || std::memcmp(bytes.data(), "DKIF", 4) ||
            std::to_integer<unsigned>(bytes[4]) != 0 || std::to_integer<unsigned>(bytes[5]) != 0 ||
            std::to_integer<unsigned>(bytes[6]) != 32 || std::to_integer<unsigned>(bytes[7]) != 0 ||
            std::memcmp(bytes.data() + 8, "VP90", 4) ||
            (std::to_integer<unsigned>(bytes[12]) | (std::to_integer<unsigned>(bytes[13]) << 8)) !=
                info.width ||
            (std::to_integer<unsigned>(bytes[14]) | (std::to_integer<unsigned>(bytes[15]) << 8)) !=
                info.height ||
            read32(bytes.data() + 16) != frame_rate || read32(bytes.data() + 20) != 1 ||
            read32(bytes.data() + 24) != info.frames)
        {
            error = "converted movie IVF header is invalid";
            return false;
        }
        std::size_t offset = 32;
        for (std::uint32_t frame = 0; frame < info.frames; ++frame)
        {
            if (bytes.size() - offset < 12)
            {
                error = "converted movie IVF frame header is incomplete";
                return false;
            }
            const auto size = read32(bytes.data() + offset);
            const auto timestamp =
                static_cast<std::uint64_t>(read32(bytes.data() + offset + 4)) |
                (static_cast<std::uint64_t>(read32(bytes.data() + offset + 8)) << 32);
            offset += 12;
            if (size == 0 || size > bytes.size() - offset || timestamp != frame)
            {
                error = "converted movie IVF frame is invalid";
                return false;
            }
            offset += size;
        }
        if (offset != bytes.size())
        {
            error = "converted movie IVF has trailing bytes";
            return false;
        }
        schema::MovieDataT movie;
        movie.video = std::make_unique<schema::MovieVideoDataT>();
        movie.video->width = info.width;
        movie.video->height = info.height;
        movie.video->frame_rate_num = frame_rate;
        movie.video->frame_rate_den = 1;
        movie.video->time_base_num = 1;
        movie.video->time_base_den = frame_rate;
        movie.video->duration_ticks = info.frames;
        movie.video->frame_count = info.frames;
        movie.video->color = std::make_unique<schema::MovieColorInfo>(
            schema::MovieColorPrimaries::bt709, schema::MovieTransfer::bt709,
            schema::MovieColorMatrix::bt709, schema::MovieColorRange::limited,
            schema::MovieChromaLocation::left);
        movie.video->payload.resize(bytes.size());
        std::memcpy(movie.video->payload.data(), bytes.data(), bytes.size());
        if (settings.audio && info.audio &&
            !audio_identity(asset, context, movie.audio_guid, error))
        {
            return false;
        }
        schema::FinishMovieDataBuffer(builder, schema::MovieData::Pack(builder, &movie));
    }
    return write_bytes(context.output / asset.guid,
                       std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                       error);
}
} // namespace engine::plugins::asset_import
