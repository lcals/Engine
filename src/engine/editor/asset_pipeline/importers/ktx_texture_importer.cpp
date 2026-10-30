#include "editor/asset_pipeline/importers/ktx_texture_importer.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "runtime/resource/ktx_texture.hpp"
#include "runtime/resource/resource_system.hpp"

#include <flatbuffers/flatbuffers.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <span>

namespace engine::editor::asset_pipeline
{
namespace
{

bool settings(const std::filesystem::path& source, const resource_detail::KtxTextureInfo& info,
              std::string& error)
{
    auto meta_path = source;
    meta_path += ".meta";
    std::string text;
    if (!ResourceSystem::read_text(meta_path, text))
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
    constexpr std::array<std::string_view, 4> fields{"format", "compression", "srgb", "mipmaps"};
    for (const auto& [key, value] : *table)
    {
        static_cast<void>(value);
        if (std::find(fields.begin(), fields.end(), key.str()) == fields.end())
        {
            error = "unknown KTX texture setting: " + std::string(key.str());
            return false;
        }
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
    return "\n[texture]\nformat = \"auto\"\ncompression = \"none\"\nsrgb = false\n";
}

bool KtxTextureImporter::inspect_batch(const ImportContext&, std::string& fingerprint,
                                       std::string& error)
{
    if (!schema_fingerprint({"texture.fbs"}, fingerprint, error))
    {
        return false;
    }
    fingerprint += ":ktx-half-float-1";
    return true;
}

bool KtxTextureImporter::convert(const ImportAsset& asset, const ImportContext& context,
                                 std::string& error) const
{
    ByteBuffer bytes;
    resource_detail::KtxTextureInfo info;
    if (!ResourceSystem::read_binary(asset.source, bytes) ||
        !resource_detail::read_ktx_info(bytes, info))
    {
        error = "invalid KTX1: requires linear RGBA16F/RG16F, 2D or cube, single/full mip chain, "
                "and at most 63 MiB";
        return false;
    }
    if (!settings(asset.source, info, error))
    {
        return false;
    }
    schema::TextureDataT texture;
    texture.path = asset.path;
    texture.width = info.width;
    texture.height = info.height;
    texture.mip_count = info.mip_count;
    texture.cube_map = info.cube_map;
    texture.srgb = false;
    texture.encoding = schema::TextureEncoding::ktx;
    texture.format = info.format;
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    texture.data.assign(data, data + bytes.size());
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishTextureDataBuffer(builder, schema::TextureData::Pack(builder, &texture));
    return write_bytes(context.output / asset.guid,
                       std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                       error);
}

} // namespace engine::editor::asset_pipeline
