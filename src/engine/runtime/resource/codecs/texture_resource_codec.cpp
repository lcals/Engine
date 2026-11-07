#include "runtime/resource/codecs/texture_resource_codec.hpp"
#include "runtime/resource/codecs/resource_decode.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/ktx_texture.hpp"
#include <set>

#include <basisu/transcoder/basisu_transcoder.h>

#include <algorithm>
#include <utility>

namespace engine
{

void TextureResourceCodec::initialize_transcoder()
{
    static const bool initialized = []
    {
        basist::basisu_transcoder_init();
        return true;
    }();
    static_cast<void>(initialized);
}

bool TextureResourceCodec::validate(const schema::TextureDataT& data)
{
    initialize_transcoder();
    if (data.schema_version != 1 || !valid_path(data.path) || data.width == 0 || data.height == 0 ||
        data.width > 16384 || data.height > 16384 || data.mip_count == 0 ||
        data.format < schema::TextureFormat::MIN || data.format > schema::TextureFormat::MAX ||
        data.data.empty() || data.data.size() > 64U * 1024U * 1024U)
    {
        return false;
    }
    if (data.encoding == schema::TextureEncoding::rgba8)
    {
        return !data.cube_map && data.mip_count == 1 &&
               (data.format == schema::TextureFormat::auto_format ||
                data.format == schema::TextureFormat::rgba8) &&
               data.data.size() == static_cast<std::uint64_t>(data.width) * data.height * 4;
    }
    if (data.encoding == schema::TextureEncoding::ktx)
    {
        resource_detail::KtxTextureInfo info;
        return !data.srgb &&
               resource_detail::read_ktx_info(std::as_bytes(std::span(data.data)), info) &&
               info.width == data.width && info.height == data.height &&
               info.mip_count == data.mip_count && info.cube_map == data.cube_map &&
               info.format == data.format;
    }
    if (data.encoding != schema::TextureEncoding::basis || data.cube_map ||
        data.format == schema::TextureFormat::rg16f)
    {
        return false;
    }
    basist::basisu_transcoder decoder;
    basist::basisu_image_info info{};
    const auto size = static_cast<std::uint32_t>(data.data.size());
    if (!decoder.validate_file_checksums(data.data.data(), size, true) ||
        decoder.get_total_images(data.data.data(), size) != 1 ||
        decoder.get_texture_type(data.data.data(), size) != basist::cBASISTexType2D ||
        !decoder.get_image_info(data.data.data(), size, info, 0))
    {
        return false;
    }
    const bool hdr =
        basist::basis_tex_format_is_hdr(decoder.get_basis_tex_format(data.data.data(), size));
    if ((hdr && (data.srgb || data.format != schema::TextureFormat::rgba16f)) ||
        (!hdr && data.format == schema::TextureFormat::rgba16f))
    {
        return false;
    }
    std::uint32_t full_levels = 1;
    for (auto dimension = std::max(data.width, data.height); dimension > 1; dimension >>= 1)
    {
        ++full_levels;
    }
    if (info.m_orig_width != data.width || info.m_orig_height != data.height ||
        info.m_total_levels != data.mip_count ||
        (data.mip_count != 1 && data.mip_count != full_levels))
    {
        return false;
    }
    for (std::uint32_t level = 0; level < data.mip_count; ++level)
    {
        basist::basisu_image_level_info mip{};
        if (!decoder.get_image_level_info(data.data.data(), size, mip, 0, level) ||
            mip.m_orig_width != std::max(1U, data.width >> level) ||
            mip.m_orig_height != std::max(1U, data.height >> level))
        {
            return false;
        }
    }
    return true;
}

schema::AssetKind TextureResourceCodec::kind() const noexcept
{
    return schema::AssetKind::texture;
}

bool TextureResourceCodec::decode(const schema::AssetEntryT& entry, ByteView payload,
                                  std::unique_ptr<Resource>& output, std::string& error) const
{
    const auto validate = [](ByteView payload, std::string& error)
    {
        return resource_decode_detail::verify_flatbuffer(payload, schema::VerifyTextureDataBuffer,
                                                         "Invalid Texture FlatBuffer", error);
    };
    return resource_decode_detail::decode_flatbuffer<ResourceType>(
        entry, payload, 64U * 1024U * 1024U, validate,
        [](const std::uint8_t* bytes) { return schema::UnPackTextureData(bytes); }, output, error);
}

bool TextureResourceCodec::validate_data(const Resource& resource, std::string& error) const
{
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data || !(resource.info().dependencies.empty() && data->path == resource.info().path &&
                   validate(*data)))
    {
        error = "Invalid Texture data or declared dependencies";
        return false;
    }
    error.clear();
    return true;
}

bool TextureResourceCodec::validate_dependencies(const Resource& resource,
                                                 ResourceView dependencies,
                                                 std::string& error) const
{
    error.clear();
    return resource.info().dependencies.empty() && dependencies.empty();
}
} // namespace engine
