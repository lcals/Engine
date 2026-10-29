#include "editor/asset_pipeline/importers/texture_importer.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/texture_generated.h"
#include "runtime/utils/path_utils.hpp"

#include <basisu/encoder/basisu_comp.h>
#include <basisu/encoder/jpgd.h>
#include <basisu/encoder/pvpngreader.h>
#include <flatbuffers/flatbuffers.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::asset_pipeline
{
namespace
{

constexpr std::uint32_t maximum_dimension = 8192;
constexpr std::uint64_t maximum_pixels = 16U * 1024U * 1024U;

struct TextureSettings
{
    schema::TextureFormat format = schema::TextureFormat::auto_format;
    bool uastc = true;
    std::uint32_t quality = 128;
    std::uint32_t compression_level = 2;
    float rdo_quality_scalar = 1.0F;
    bool srgb = true;
    bool mipmaps = true;
};

bool dimensions(std::uint32_t width, std::uint32_t height)
{
    return width != 0 && height != 0 && width <= maximum_dimension && height <= maximum_dimension &&
           static_cast<std::uint64_t>(width) * height <= maximum_pixels;
}

bool settings(const std::filesystem::path& source, TextureSettings& output, std::string& error)
{
    auto path = source;
    path += ".meta";
    std::string text;
    if (!ResourceSystem::read_text(path, text))
    {
        error = "cannot read texture meta";
        return false;
    }
    auto parsed = toml::parse(text);
    if (!parsed)
    {
        error = "invalid texture meta TOML";
        return false;
    }
    const auto& root = parsed.table();
    if (!root.contains("texture"))
    {
        return true;
    }
    const auto* table = root["texture"].as_table();
    if (table == nullptr)
    {
        error = "texture meta requires a [texture] table";
        return false;
    }
    constexpr std::array<std::string_view, 7> fields = {
        "format", "compression", "quality", "compression_level", "rdo_quality_scalar",
        "srgb",   "mipmaps"};
    for (const auto& [key, value] : *table)
    {
        static_cast<void>(value);
        if (std::find(fields.begin(), fields.end(), key.str()) == fields.end())
        {
            error = "unknown texture meta setting: " + std::string(key.str());
            return false;
        }
    }
    const auto format = (*table)["format"].value_or(std::string("auto"));
    constexpr std::array<std::string_view, 5> formats = {"auto", "rgba8", "bc7", "astc4x4", "etc2"};
    const auto selected = std::find(formats.begin(), formats.end(), format);
    const auto compression = (*table)["compression"].value_or(std::string("uastc"));
    if ((table->contains("format") && !(*table)["format"].is_string()) ||
        (table->contains("compression") && !(*table)["compression"].is_string()) ||
        selected == formats.end() || (compression != "uastc" && compression != "etc1s"))
    {
        error = "texture format/compression is unsupported";
        return false;
    }
    output.format = static_cast<schema::TextureFormat>(selected - formats.begin());
    output.uastc = compression == "uastc";
    if ((output.uastc && table->contains("quality")) ||
        (!output.uastc && table->contains("rdo_quality_scalar")))
    {
        error = "quality applies to ETC1S; rdo_quality_scalar applies to UASTC";
        return false;
    }
    const auto integer =
        [&](const char* name, std::uint32_t minimum, std::uint32_t maximum, std::uint32_t& result)
    {
        if (!table->contains(name))
        {
            return true;
        }
        const auto value = (*table)[name].value<std::int64_t>();
        if (!(*table)[name].is_integer() || !value || *value < minimum || *value > maximum)
        {
            error = "texture setting is out of range: " + std::string(name);
            return false;
        }
        result = static_cast<std::uint32_t>(*value);
        return true;
    };
    if (!integer("quality", 1, 255, output.quality) ||
        !integer("compression_level", 0, output.uastc ? 4U : 5U, output.compression_level))
    {
        return false;
    }
    if (table->contains("rdo_quality_scalar"))
    {
        const auto value = (*table)["rdo_quality_scalar"].value<double>();
        if (!value || !std::isfinite(*value) || *value < 0.001 || *value > 50.0)
        {
            error = "rdo_quality_scalar must be a finite number in [0.001, 50]";
            return false;
        }
        output.rdo_quality_scalar = static_cast<float>(*value);
    }
    for (const char* name : {"srgb", "mipmaps"})
    {
        if (table->contains(name) && !(*table)[name].is_boolean())
        {
            error = "texture setting must be boolean: " + std::string(name);
            return false;
        }
    }
    output.srgb = (*table)["srgb"].value_or(true);
    output.mipmaps = (*table)["mipmaps"].value_or(true);
    return true;
}

bool read_image(const ImportAsset& asset, basisu::image& image, std::string& error)
{
    ByteBuffer bytes;
    if (!ResourceSystem::read_binary(asset.source, bytes) || bytes.empty() ||
        bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        error = "cannot read texture source";
        return false;
    }
    auto extension = PathUtils::to_utf8(asset.source.extension());
    std::transform(
        extension.begin(), extension.end(), extension.begin(), [](unsigned char value)
        { return static_cast<char>(value >= 'A' && value <= 'Z' ? value + 32 : value); });
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    if (extension == ".png")
    {
        pv_png::png_info info{};
        if (!pv_png::get_png_info(data, bytes.size(), info) ||
            !dimensions(info.m_width, info.m_height))
        {
            error = "invalid PNG header or unsupported texture dimensions";
            return false;
        }
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t channels = 0;
        void* pixels = pv_png::load_png(data, bytes.size(), 4, width, height, channels);
        if (pixels == nullptr || width != info.m_width || height != info.m_height)
        {
            std::free(pixels);
            error = "cannot decode PNG texture";
            return false;
        }
        image.init(static_cast<const std::uint8_t*>(pixels), width, height, 4);
        std::free(pixels);
        return true;
    }
    if (extension == ".jpg" || extension == ".jpeg")
    {
        jpgd::jpeg_decoder_mem_stream stream(data, static_cast<std::uint32_t>(bytes.size()));
        jpgd::jpeg_decoder header(&stream);
        if (header.get_error_code() != jpgd::JPGD_SUCCESS || header.get_width() <= 0 ||
            header.get_height() <= 0 ||
            !dimensions(static_cast<std::uint32_t>(header.get_width()),
                        static_cast<std::uint32_t>(header.get_height())))
        {
            error = "invalid JPEG header or unsupported texture dimensions";
            return false;
        }
        int width = 0;
        int height = 0;
        int channels = 0;
        auto* pixels = jpgd::decompress_jpeg_image_from_memory(
            data, static_cast<int>(bytes.size()), &width, &height, &channels, 4,
            jpgd::jpeg_decoder::cFlagLinearChromaFiltering);
        if (pixels == nullptr || width != header.get_width() || height != header.get_height())
        {
            std::free(pixels);
            error = "cannot decode JPEG texture";
            return false;
        }
        image.init(pixels, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                   4);
        std::free(pixels);
        return true;
    }
    error = "texture source must be PNG, JPG or JPEG";
    return false;
}

} // namespace

std::string_view TextureImporter::default_meta() const noexcept
{
    return "\n[texture]\nformat = \"auto\"\ncompression = \"uastc\"\n"
           "compression_level = 2\nrdo_quality_scalar = 1.0\nsrgb = true\nmipmaps = true\n";
}

std::string_view TextureImporter::version() noexcept
{
    return "basisu-" BASISU_LIB_VERSION_STRING "-texture-importer-1";
}

bool TextureImporter::initialize(std::string& error)
{
    if (!basisu::basisu_encoder_init())
    {
        error = "cannot initialize BasisU texture encoder";
        return false;
    }
    return true;
}

bool TextureImporter::convert(const ImportAsset& asset, const ImportContext& context,
                              std::string& error) const
{
    TextureSettings configuration;
    basisu::image image;
    if (!settings(asset.source, configuration, error) || !read_image(asset, image, error))
    {
        return false;
    }
    basisu::job_pool jobs(1);
    basisu::basis_compressor_params parameters;
    parameters.m_pJob_pool = &jobs;
    parameters.m_source_images.push_back(image);
    parameters.m_uastc = configuration.uastc;
    parameters.m_quality_level = static_cast<int>(configuration.quality);
    parameters.m_compression_level = static_cast<int>(configuration.compression_level);
    parameters.m_pack_uastc_flags = configuration.compression_level;
    parameters.m_rdo_uastc = configuration.uastc;
    parameters.m_rdo_uastc_quality_scalar = configuration.rdo_quality_scalar;
    parameters.m_mip_gen = configuration.mipmaps;
    parameters.m_mip_srgb = configuration.srgb;
    parameters.m_perceptual = configuration.srgb;
    parameters.m_multithreading = false;
    parameters.m_rdo_uastc_multithreading = false;
    parameters.m_status_output = false;
    parameters.m_print_stats = false;
    parameters.m_debug = false;
    parameters.m_write_output_basis_or_ktx2_files = false;
    basisu::basis_compressor compressor;
    if (!compressor.init(parameters) ||
        compressor.process() != basisu::basis_compressor::cECSuccess)
    {
        error = "BasisU texture compression failed";
        return false;
    }
    const auto& encoded = compressor.get_output_basis_file();
    basist::basisu_transcoder transcoder;
    basist::basisu_image_info info{};
    if (encoded.empty() || !transcoder.get_image_info(encoded.data(), encoded.size(), info, 0))
    {
        error = "BasisU produced invalid texture data";
        return false;
    }
    schema::TextureDataT texture;
    texture.path = asset.path;
    texture.width = info.m_orig_width;
    texture.height = info.m_orig_height;
    texture.mip_count = info.m_total_levels;
    texture.srgb = configuration.srgb;
    texture.encoding = schema::TextureEncoding::basis;
    texture.format = configuration.format;
    texture.data.assign(encoded.begin(), encoded.end());
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishTextureDataBuffer(builder, schema::TextureData::Pack(builder, &texture));
    return write_bytes(context.output / asset.guid,
                       std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                       error);
}

schema::AssetKind TextureImporter::kind() const noexcept
{
    return schema::AssetKind::texture;
}
bool TextureImporter::matches(const std::filesystem::path& source) const
{
    const auto extension = source_extension(source);
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
}
bool TextureImporter::inspect_batch(const ImportContext&, std::string& fingerprint,
                                    std::string& error)
{
    if (!schema_fingerprint({"texture.fbs"}, fingerprint, error))
    {
        return false;
    }
    fingerprint += ":" + std::string(version());
    return true;
}
bool TextureImporter::prepare_batch(const ImportContext&, TaskDescription& task,
                                    std::string& error) const
{
    task = {};
    return initialize(error);
}

} // namespace engine::asset_pipeline
