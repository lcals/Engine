#include "editor/asset_pipeline/importers/texture_importer.hpp"
#include "editor/file_text.hpp"
#include "runtime/resource/virtual_file_system.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "runtime/resource/codecs/texture_resource_codec.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/texture_generated.h"
#include "runtime/utils/path_utils.hpp"

#include <basisu/encoder/3rdparty/tinyexr.h>
#include <basisu/encoder/basisu_comp.h>
#include <basisu/encoder/jpgd.h>
#include <basisu/encoder/pvpngreader.h>
#include <basisu/transcoder/basisu_astc_hdr_core.h>
#include <flatbuffers/flatbuffers.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
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

namespace engine::editor::asset_pipeline
{
namespace
{

constexpr std::uint32_t maximum_dimension = 8192;
constexpr std::uint64_t maximum_pixels = 16U * 1024U * 1024U;

struct TextureSettings
{
    schema::TextureFormat format = schema::TextureFormat::auto_format;
    bool native = false;
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

bool settings(const std::filesystem::path& source, bool hdr, TextureSettings& output,
              std::string& error)
{
    output.srgb = !hdr;
    output.format = hdr ? schema::TextureFormat::rgba16f : schema::TextureFormat::auto_format;
    auto path = source;
    path += ".meta";
    std::string text;
    if (!engine::editor::file_detail::read_text(path, text))
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
    constexpr std::array<std::string_view, 8> fields = {
        "format", "compression", "quality", "compression_level", "rdo_quality_scalar",
        "srgb",   "mipmaps",     "storage"};
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
    constexpr std::array<std::string_view, 6> formats = {"auto",    "rgba8", "bc7",
                                                         "astc4x4", "etc2",  "rgba16f"};
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
    const auto storage = (*table)["storage"].value_or(std::string("basis"));
    if ((table->contains("storage") && !(*table)["storage"].is_string()) ||
        (storage != "basis" && storage != "native"))
    {
        error = "texture storage must be basis or native";
        return false;
    }
    output.native = storage == "native";
    output.uastc = compression == "uastc";
    if ((hdr && (!output.uastc || (output.format != schema::TextureFormat::auto_format &&
                                   output.format != schema::TextureFormat::rgba16f))) ||
        (!hdr && output.format == schema::TextureFormat::rgba16f))
    {
        error = "HDR sources require UASTC and auto/rgba16f; LDR sources require an LDR format";
        return false;
    }
    if (hdr)
    {
        output.format = schema::TextureFormat::rgba16f;
        if (table->contains("rdo_quality_scalar"))
        {
            error = "rdo_quality_scalar applies only to UASTC LDR";
            return false;
        }
    }
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
        !integer("compression_level", 0,
                 output.uastc ? 4U : basisu::BASISU_MAX_ETC1S_COMPRESSION_LEVEL,
                 output.compression_level))
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
    output.srgb = (*table)["srgb"].value_or(!hdr);
    if (hdr && output.srgb)
    {
        error = "HDR textures require linear color space (srgb=false)";
        return false;
    }
    output.mipmaps = (*table)["mipmaps"].value_or(true);
    return true;
}

bool native_texture(schema::TextureDataT& texture, std::span<const std::uint8_t> encoded,
                    std::string_view platform, std::string& error)
{
    if (texture.format == schema::TextureFormat::auto_format)
    {
        if (platform == "windows" || platform == "linux")
        {
            texture.format = schema::TextureFormat::bc7;
        }
        else if (platform == "macos" || platform == "ios")
        {
            texture.format = schema::TextureFormat::astc4x4;
        }
        else if (platform == "android")
        {
            texture.format = schema::TextureFormat::etc2;
        }
        else
        {
            error = "native texture requires an explicit target platform";
            return false;
        }
    }
    using Target = basist::transcoder_texture_format;
    Target target;
    switch (texture.format)
    {
    case schema::TextureFormat::bc7:
        target = Target::cTFBC7_RGBA;
        break;
    case schema::TextureFormat::astc4x4:
        target = Target::cTFASTC_4x4_RGBA;
        break;
    case schema::TextureFormat::etc2:
        target = Target::cTFETC2_RGBA;
        break;
    case schema::TextureFormat::rgba8:
        target = Target::cTFRGBA32;
        break;
    case schema::TextureFormat::rgba16f:
        target = Target::cTFRGBA_HALF;
        break;
    default:
        error = "unsupported native texture format";
        return false;
    }
    basist::basisu_transcoder decoder;
    const auto size = static_cast<std::uint32_t>(encoded.size());
    if (!decoder.start_transcoding(encoded.data(), size))
    {
        error = "cannot initialize native texture transcoding";
        return false;
    }
    const bool pixels = texture.format == schema::TextureFormat::rgba8 ||
                        texture.format == schema::TextureFormat::rgba16f;
    for (std::uint32_t mip = 0; mip < texture.mip_count; ++mip)
    {
        const auto width = std::max(1U, texture.width >> mip);
        const auto height = std::max(1U, texture.height >> mip);
        const auto units = pixels ? static_cast<std::size_t>(width) * height
                                  : static_cast<std::size_t>((width + 3) / 4) * ((height + 3) / 4);
        const auto bytes = units * (texture.format == schema::TextureFormat::rgba16f ? 8U
                                    : pixels                                         ? 4U
                                                                                     : 16U);
        constexpr std::size_t maximum_bytes = 63U * 1024U * 1024U;
        if (bytes > maximum_bytes - texture.data.size())
        {
            error = "native texture exceeds the resource limit; select basis or reduce the image";
            return false;
        }
        const auto offset = texture.data.size();
        texture.data.resize(offset + bytes);
        if (!decoder.transcode_image_level(encoded.data(), size, 0, mip,
                                           texture.data.data() + offset,
                                           static_cast<std::uint32_t>(units), target))
        {
            error = "cannot transcode native texture mip";
            return false;
        }
        texture.subresources.emplace_back(mip, 0, offset, bytes);
    }
    texture.encoding = schema::TextureEncoding::native;
    return true;
}

bool read_image(std::span<const std::uint8_t> bytes, std::string_view extension,
                basisu::image& image, std::string& error)
{
    const auto* data = bytes.data();
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
    if (extension == ".tga")
    {
        const auto u16 = [&](std::size_t offset)
        { return std::uint32_t(data[offset]) | (std::uint32_t(data[offset + 1]) << 8); };
        if (bytes.size() < 18 || !dimensions(u16(12), u16(14)))
        {
            error = "invalid TGA header or unsupported texture dimensions";
            return false;
        }
        int width = 0;
        int height = 0;
        int channels = 0;
        auto* pixels = basisu::read_tga(data, static_cast<std::uint32_t>(bytes.size()), width,
                                        height, channels);
        if (pixels == nullptr || width != static_cast<int>(u16(12)) ||
            height != static_cast<int>(u16(14)))
        {
            std::free(pixels);
            error = "cannot decode TGA texture";
            return false;
        }
        image.init(pixels, width, height, channels);
        std::free(pixels);
        return true;
    }
    if (extension == ".qoi")
    {
        const auto u32 = [&](std::size_t offset)
        {
            return (std::uint32_t(data[offset]) << 24) | (std::uint32_t(data[offset + 1]) << 16) |
                   (std::uint32_t(data[offset + 2]) << 8) | std::uint32_t(data[offset + 3]);
        };
        if (bytes.size() < 14 || !dimensions(u32(4), u32(8)) ||
            !basisu::load_qoi(data, bytes.size(), image))
        {
            error = "invalid QOI texture or unsupported dimensions";
            return false;
        }
        return true;
    }
    error = "unsupported LDR texture source";
    return false;
}

bool read_hdr(std::span<const std::uint8_t> bytes, std::string_view extension,
              basisu::imagef& image, std::string& error)
{
    bool luminance = false;
    if (extension == ".exr")
    {
        EXRVersion version{};
        EXRHeader header{};
        InitEXRHeader(&header);
        const char* message = nullptr;
        const bool parsed =
            ParseEXRVersionFromMemory(&version, bytes.data(), bytes.size()) == 0 &&
            !version.multipart && !version.non_image &&
            ParseEXRHeaderFromMemory(&header, &version, bytes.data(), bytes.size(), &message) == 0;
        const auto width = std::int64_t(header.data_window.max_x) - header.data_window.min_x + 1;
        const auto height = std::int64_t(header.data_window.max_y) - header.data_window.min_y + 1;
        const bool valid = parsed && width > 0 && height > 0 && width <= maximum_dimension &&
                           height <= maximum_dimension && dimensions(width, height);
        luminance =
            parsed && header.num_channels == 1 && std::string_view(header.channels[0].name) == "Y";
        FreeEXRErrorMessage(message);
        FreeEXRHeader(&header);
        if (!valid)
        {
            error = "invalid EXR header or unsupported texture dimensions/type";
            return false;
        }
    }
    else
    {
        // Bound the Radiance resolution before the upstream decoder allocates image storage.
        const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        const auto separator = text.find("\n\n");
        if (separator == std::string_view::npos || separator > 4096)
        {
            error = "invalid HDR header";
            return false;
        }
        auto resolution = text.substr(separator + 2);
        const auto end = resolution.find('\n');
        if (end == std::string_view::npos || end > 128)
        {
            error = "invalid HDR resolution";
            return false;
        }
        resolution = resolution.substr(0, end);
        std::array<std::uint32_t, 2> sizes{};
        for (auto& size : sizes)
        {
            while (!resolution.empty() && resolution.front() == ' ')
            {
                resolution.remove_prefix(1);
            }
            if (resolution.size() < 4 || (resolution[0] != '-' && resolution[0] != '+') ||
                (resolution[1] != 'X' && resolution[1] != 'Y') || resolution[2] != ' ')
            {
                error = "invalid HDR resolution axis";
                return false;
            }
            resolution.remove_prefix(3);
            const auto parsed =
                std::from_chars(resolution.data(), resolution.data() + resolution.size(), size);
            if (parsed.ec != std::errc{})
            {
                error = "invalid HDR resolution size";
                return false;
            }
            resolution.remove_prefix(static_cast<std::size_t>(parsed.ptr - resolution.data()));
        }
        if (!resolution.empty() || !dimensions(sizes[0], sizes[1]))
        {
            error = "unsupported HDR texture dimensions";
            return false;
        }
    }
    const auto type = extension == ".exr" ? basisu::hdr_image_type::cHITEXRImage
                                          : basisu::hdr_image_type::cHITHDRImage;
    if (!basisu::load_image_hdr(bytes.data(), bytes.size(), image, 0, 0, type, false) ||
        !dimensions(image.get_width(), image.get_height()))
    {
        error = "cannot decode HDR texture";
        return false;
    }
    for (std::uint32_t index = 0; index < image.get_total_pixels(); ++index)
    {
        auto& pixel = image.get_ptr()[index];
        if (luminance)
        {
            pixel[3] = 1.0F;
        }
        if (pixel[3] != 1.0F)
        {
            error = "BasisU HDR does not preserve alpha; use linear RGBA16F KTX for HDR alpha";
            return false;
        }
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            if (!std::isfinite(pixel[channel]) || pixel[channel] < 0.0F ||
                pixel[channel] > basist::ASTC_HDR_MAX_VAL)
            {
                error = "BasisU HDR requires finite RGB values in [0, 65216]";
                return false;
            }
        }
    }
    return true;
}

} // namespace

std::string_view TextureImporter::default_meta() const noexcept
{
    return "\n[texture]\nstorage = \"basis\"\nformat = \"auto\"\ncompression = \"uastc\"\n"
           "compression_level = 2\nmipmaps = true\n";
}

std::string_view TextureImporter::version() noexcept
{
    // TinyEXR 3.3 and stb 2026-08-02 also change decoding, independent of the BasisU version.
    return "basisu-" BASISU_LIB_VERSION_STRING "-texture-importer-4-native-map-1";
}

bool TextureImporter::initialize(std::string& error)
{
    TextureResourceCodec::initialize_transcoder();
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
    const auto extension = source_extension(asset.source);
    const bool hdr = extension == ".hdr" || extension == ".exr";
    ByteBuffer source;
    if (!settings(asset.source, hdr, configuration, error))
    {
        return false;
    }
    if (!VirtualFileSystem::read_file(asset.source, source) || source.empty() ||
        source.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        error = "cannot read texture source";
        return false;
    }
    const auto bytes =
        std::span(reinterpret_cast<const std::uint8_t*>(source.data()), source.size());
    basisu::job_pool jobs(1);
    basisu::basis_compressor_params parameters;
    parameters.m_pJob_pool = &jobs;
    if (hdr)
    {
        basisu::imagef image;
        if (!read_hdr(bytes, extension, image, error))
        {
            return false;
        }
        parameters.m_source_images_hdr.push_back(std::move(image));
        parameters.set_format_mode(basist::basis_tex_format::cUASTC_HDR_4x4);
        parameters.m_uastc_hdr_4x4_options.set_quality_level(configuration.compression_level);
    }
    else
    {
        basisu::image image;
        if (!read_image(bytes, extension, image, error))
        {
            return false;
        }
        parameters.m_source_images.push_back(std::move(image));
        parameters.set_format_mode(configuration.uastc ? basist::basis_tex_format::cUASTC_LDR_4x4
                                                       : basist::basis_tex_format::cETC1S);
        parameters.m_quality_level = static_cast<int>(configuration.quality);
        parameters.m_etc1s_compression_level = static_cast<int>(configuration.compression_level);
        parameters.m_pack_uastc_ldr_4x4_flags = configuration.compression_level;
        parameters.m_rdo_uastc_ldr_4x4 = configuration.uastc;
        parameters.m_rdo_uastc_ldr_4x4_quality_scalar = configuration.rdo_quality_scalar;
    }
    parameters.m_mip_gen = configuration.mipmaps;
    parameters.set_srgb_options(configuration.srgb);
    parameters.m_multithreading = false;
    parameters.m_rdo_uastc_ldr_4x4_multithreading = false;
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
    texture.schema_version = 2;
    texture.path = asset.path;
    texture.width = info.m_orig_width;
    texture.height = info.m_orig_height;
    texture.mip_count = info.m_total_levels;
    texture.srgb = configuration.srgb;
    texture.encoding = schema::TextureEncoding::basis;
    texture.format = configuration.format;
    if (configuration.native)
    {
        if (!native_texture(texture, {encoded.data(), encoded.size()}, context.target_platform,
                            error))
        {
            return false;
        }
    }
    else
    {
        texture.data.assign(encoded.begin(), encoded.end());
    }
    if (!TextureResourceCodec::validate(texture))
    {
        error = "texture import produced invalid current-version data";
        return false;
    }
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
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
           extension == ".tga" || extension == ".qoi" || extension == ".hdr" || extension == ".exr";
}
bool TextureImporter::inspect_batch(const ImportContext& context, std::string& fingerprint,
                                    std::string& error)
{
    if (!schema_fingerprint({"texture.fbs"}, fingerprint, error))
    {
        return false;
    }
    fingerprint += ":" + std::string(version()) + ":" + context.target_platform;
    return true;
}
bool TextureImporter::prepare_batch(const ImportContext&, TaskDescription& task,
                                    std::string& error) const
{
    task = {};
    return initialize(error);
}

} // namespace engine::editor::asset_pipeline
