#include "runtime/resource/codecs/texture_resource_codec.hpp"

#include "runtime/resource/resource_catalog.hpp"
#include "runtime/resource/resource_system.hpp"

#include <basisu/transcoder/basisu_transcoder.h>
#include <flatbuffers/verifier.h>

#include <algorithm>
#include <utility>

namespace engine
{

bool TextureResourceCodec::validate(const schema::TextureDataT& data)
{
    static const bool initialized = []
    {
        basist::basisu_transcoder_init();
        return true;
    }();
    static_cast<void>(initialized);
    if (data.schema_version != 1 || !valid_path(data.path) || data.width == 0 || data.height == 0 ||
        data.width > 16384 || data.height > 16384 || data.mip_count == 0 ||
        data.format < schema::TextureFormat::MIN || data.format > schema::TextureFormat::MAX ||
        data.data.empty() || data.data.size() > 64U * 1024U * 1024U)
    {
        return false;
    }
    if (data.encoding == schema::TextureEncoding::rgba8)
    {
        return data.mip_count == 1 &&
               (data.format == schema::TextureFormat::auto_format ||
                data.format == schema::TextureFormat::rgba8) &&
               data.data.size() == static_cast<std::uint64_t>(data.width) * data.height * 4;
    }
    if (data.encoding != schema::TextureEncoding::basis)
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

bool TextureResourceCodec::decode(const ResourceCatalog&, ByteView payload, Resource& output) const
{
    const auto& entry = output.info();
    std::unique_ptr<schema::TextureDataT> data;
    if (entry.path.starts_with("builtin/"))
    {
        const auto* builtin = ResourceSystem::find_texture(entry.path);
        if (!builtin)
        {
            return false;
        }
        data = std::make_unique<schema::TextureDataT>(*builtin);
    }
    else
    {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        flatbuffers::Verifier verifier(bytes, payload.size());
        if (!schema::VerifyTextureDataBuffer(verifier))
        {
            return false;
        }
        data = schema::UnPackTextureData(bytes);
    }
    if (data->path != entry.path || !validate(*data))
    {
        return false;
    }
    store(output, std::move(data));
    return true;
}

} // namespace engine
