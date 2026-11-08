#include "editor/asset_pipeline/importers/ktx_texture_importer.hpp"
#include "editor/file_text.hpp"
#include "runtime/resource/virtual_file_system.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "runtime/resource/codecs/texture_resource_codec.hpp"
#include "runtime/resource/resource_system.hpp"

#include <bimg/bimg.h>
#include <bx/error.h>
#include <flatbuffers/flatbuffers.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <span>

namespace engine::editor::asset_pipeline
{
namespace
{

std::uint32_t uint32_at(ByteView bytes, std::size_t offset) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

bool decode_ktx(ByteView bytes, schema::TextureDataT& output)
{
    output = {};
    constexpr std::array<std::uint8_t, 12> identifier{0xab, 0x4b, 0x54, 0x58, 0x20, 0x31,
                                                      0x31, 0xbb, 0x0d, 0x0a, 0x1a, 0x0a};
    if (bytes.size() < 64 || bytes.size() > 63U * 1024U * 1024U ||
        !std::equal(identifier.begin(), identifier.end(), bytes.begin(),
                    [](std::uint8_t a, std::byte b)
                    { return a == std::to_integer<std::uint8_t>(b); }))
    {
        return false;
    }
    // 当前资源只接入标准小端 KTX1、无压缩半浮点二维或立方体；先检查原始
    // 头字段，防止上游将非法面数/大整数截断为看似有效的容器信息。
    const auto format = uint32_at(bytes, 28);
    const bool rgba = format == 0x881aU;
    const bool rg = format == 0x822fU;
    const std::uint32_t channels = rgba ? 4U : 2U;
    const auto width = uint32_at(bytes, 36);
    const auto height = uint32_at(bytes, 40);
    const auto faces = uint32_at(bytes, 52);
    const auto mips = uint32_at(bytes, 56);
    const auto metadata_size = uint32_at(bytes, 60);
    const bool typed_pixels = uint32_at(bytes, 16) == 0x140bU && uint32_at(bytes, 20) == 2 &&
                              uint32_at(bytes, 24) == (rgba ? 0x1908U : 0x8227U);
    // 当前 bimg 写入器统一采用这组字段；实际半浮点布局由 internalFormat 决定。
    const bool bimg_pixels =
        uint32_at(bytes, 16) == 0 && uint32_at(bytes, 20) == 1 && uint32_at(bytes, 24) == 0;
    if (uint32_at(bytes, 12) != 0x04030201U || (!typed_pixels && !bimg_pixels) || (!rgba && !rg) ||
        uint32_at(bytes, 32) != (rgba ? 0x1908U : 0x8227U) || width == 0 || height == 0 ||
        width > 16384 || height > 16384 || uint32_at(bytes, 44) != 0 || uint32_at(bytes, 48) != 0 ||
        (faces != 1 && faces != 6) || (faces == 6 && width != height) || mips == 0 ||
        metadata_size % 4 != 0 || metadata_size > bytes.size() - 64)
    {
        return false;
    }
    std::uint32_t full_mips = 1;
    for (auto dimension = std::max(width, height); dimension > 1; dimension >>= 1)
    {
        ++full_mips;
    }
    if (mips != 1 && mips != full_mips)
    {
        return false;
    }
    std::size_t offset = 64 + metadata_size;
    for (std::uint32_t mip = 0; mip < mips; ++mip)
    {
        const auto face_bytes = static_cast<std::uint64_t>(std::max(1U, width >> mip)) *
                                std::max(1U, height >> mip) * channels * 2U;
        if (offset > bytes.size() || bytes.size() - offset < 4 ||
            uint32_at(bytes, offset) != face_bytes)
        {
            return false;
        }
        offset += 4;
        const auto mip_bytes = face_bytes * faces;
        if (mip_bytes > bytes.size() - offset)
        {
            return false;
        }
        offset += static_cast<std::size_t>(mip_bytes);
    }
    if (offset != bytes.size())
    {
        return false;
    }
    // bimg API 167 preserves the KTX dimension convention: 2D/cube depth is zero.
    bimg::ImageContainer image{};
    bx::Error error;
    if (!bimg::imageParse(image, bytes.data(), static_cast<std::uint32_t>(bytes.size()), &error) ||
        !error.isOk() || !image.m_ktx || image.m_srgb || image.m_width != width ||
        image.m_height != height || image.m_depth != 0 || image.m_numLayers != 1 ||
        image.m_numMips != mips || image.m_cubeMap != (faces == 6) ||
        image.m_format != (rgba ? bimg::TextureFormat::RGBA16F : bimg::TextureFormat::RG16F))
    {
        return false;
    }
    output.width = width;
    output.height = height;
    output.mip_count = mips;
    output.cube_map = faces == 6;
    output.format = rgba ? schema::TextureFormat::rgba16f : schema::TextureFormat::rg16f;
    output.schema_version = 2;
    output.encoding = schema::TextureEncoding::native;
    output.srgb = false;
    offset = 64 + metadata_size;
    for (std::uint32_t mip = 0; mip < mips; ++mip)
    {
        const auto face_bytes = uint32_at(bytes, offset);
        offset += 4;
        for (std::uint32_t face = 0; face < faces; ++face)
        {
            output.subresources.emplace_back(mip, face, output.data.size(), face_bytes);
            const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data() + offset);
            output.data.insert(output.data.end(), data, data + face_bytes);
            offset += face_bytes;
        }
    }
    return true;
}

bool settings(const std::filesystem::path& source, const schema::TextureDataT& info,
              std::string& error)
{
    auto meta_path = source;
    meta_path += ".meta";
    std::string text;
    if (!engine::editor::file_detail::read_text(meta_path, text))
    {
        error = "cannot read KTX texture meta";
        return false;
    }
    auto parsed = toml::parse(text);
    if (!parsed)
    {
        error = "invalid KTX texture meta TOML";
        return false;
    }
    const auto* table = parsed.table()["texture"].as_table();
    if (table == nullptr)
    {
        error = "KTX meta requires [texture] with compression=none and srgb=false";
        return false;
    }
    constexpr std::array<std::string_view, 5> fields{"format", "compression", "srgb", "mipmaps",
                                                     "storage"};
    for (const auto& [key, value] : *table)
    {
        static_cast<void>(value);
        if (std::find(fields.begin(), fields.end(), key.str()) == fields.end())
        {
            error = "unknown KTX texture setting: " + std::string(key.str());
            return false;
        }
    }
    if (table->contains("storage") && (*table)["storage"].value<std::string_view>() != "native")
    {
        error = "KTX texture storage must be native";
        return false;
    }
    const auto compression = (*table)["compression"].value<std::string_view>();
    const auto srgb = (*table)["srgb"].value_exact<bool>();
    if (compression != "none" || !srgb || *srgb)
    {
        error = "HDR KTX textures require compression=none and srgb=false";
        return false;
    }
    if (table->contains("format"))
    {
        const auto format = (*table)["format"].value<std::string_view>();
        const auto expected = info.format == schema::TextureFormat::rgba16f ? "rgba16f" : "rg16f";
        if (format != "auto" && format != expected)
        {
            error = "KTX format must be auto or match the existing half-float container";
            return false;
        }
    }
    if (table->contains("mipmaps"))
    {
        const auto mipmaps = (*table)["mipmaps"].value_exact<bool>();
        if (!mipmaps || *mipmaps != (info.mip_count > 1))
        {
            error = "KTX mipmaps must match the existing container; import never regenerates mips";
            return false;
        }
    }
    return true;
}

} // namespace

std::string_view KtxTextureImporter::id() const noexcept
{
    return "texture_ktx";
}

schema::AssetKind KtxTextureImporter::kind() const noexcept
{
    return schema::AssetKind::texture;
}

bool KtxTextureImporter::matches(const std::filesystem::path& source) const
{
    return source_extension(source) == ".ktx";
}

std::string_view KtxTextureImporter::default_meta() const noexcept
{
    return "\n[texture]\nstorage = \"native\"\nformat = \"auto\"\ncompression = \"none\"\nsrgb = "
           "false\n";
}

bool KtxTextureImporter::inspect_batch(const ImportContext&, std::string& fingerprint,
                                       std::string& error)
{
    if (!schema_fingerprint({"texture.fbs"}, fingerprint, error))
    {
        return false;
    }
    fingerprint += ":ktx-native-half-float-2";
    return true;
}

bool KtxTextureImporter::convert(const ImportAsset& asset, const ImportContext& context,
                                 std::string& error) const
{
    ByteBuffer bytes;
    schema::TextureDataT texture;
    if (!VirtualFileSystem::read_file(asset.source, bytes) || !decode_ktx(bytes, texture))
    {
        error = "invalid KTX1: requires linear RGBA16F/RG16F, 2D or cube, single/full mip chain, "
                "and at most 63 MiB";
        return false;
    }
    if (!settings(asset.source, texture, error))
    {
        return false;
    }
    texture.path = asset.path;
    if (!TextureResourceCodec::validate(texture))
    {
        error = "invalid native texture extracted from KTX";
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishTextureDataBuffer(builder, schema::TextureData::Pack(builder, &texture));
    return write_bytes(context.output / asset.guid,
                       std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                       error);
}

} // namespace engine::editor::asset_pipeline
