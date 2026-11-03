#include "runtime/render/resources/gpu_texture.hpp"

#include "runtime/performance/profiling.hpp"
#include "runtime/resource/resource.hpp"

#include <basisu/transcoder/basisu_transcoder.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace engine::render_detail
{

GpuTexture::~GpuTexture() noexcept
{
    release();
}

GpuTexture::GpuTexture(GpuTexture&& other) noexcept
    : resource(std::move(other.resource)),
      handle(std::exchange(other.handle, bgfx::TextureHandle{bgfx::kInvalidHandle}))
{
}

GpuTexture& GpuTexture::operator=(GpuTexture&& other) noexcept
{
    if (this != &other)
    {
        release();
        resource = std::move(other.resource);
        handle = std::exchange(other.handle, bgfx::TextureHandle{bgfx::kInvalidHandle});
    }
    return *this;
}

bool PreparedTexture::describe(const ResourcePtr& source, const bgfx::Caps& caps,
                               std::size_t max_bytes)
{
    *this = {};
    const auto* data = source ? source->texture() : nullptr;
    if (data == nullptr || data->width == 0 || data->height == 0 ||
        data->width > caps.limits.maxTextureSize || data->height > caps.limits.maxTextureSize ||
        data->width > std::numeric_limits<std::uint16_t>::max() ||
        data->height > std::numeric_limits<std::uint16_t>::max() ||
        data->data.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }
    std::uint32_t full_levels = 1;
    for (auto size = std::max(data->width, data->height); size > 1; size >>= 1)
    {
        ++full_levels;
    }
    if (data->mip_count != 1 && data->mip_count != full_levels)
    {
        return false;
    }
    PreparedTexture candidate;
    candidate.resource = source;
    using Format = bgfx::TextureFormat;
    if (data->encoding == schema::TextureEncoding::ktx)
    {
        candidate.format = data->format == schema::TextureFormat::rgba16f ? Format::RGBA16F
                                                                          : Format::RG16F;
        const auto required =
            data->cube_map ? BGFX_CAPS_FORMAT_TEXTURE_CUBE : BGFX_CAPS_FORMAT_TEXTURE_2D;
        if (data->srgb || data->data.empty() || data->data.size() > max_bytes ||
            (data->format != schema::TextureFormat::rgba16f &&
             data->format != schema::TextureFormat::rg16f) ||
            (caps.formats[candidate.format] & required) == 0)
        {
            return false;
        }
        candidate.flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
        candidate.bytes = data->data.size();
        *this = std::move(candidate);
        return true;
    }
    if (data->encoding != schema::TextureEncoding::basis &&
        data->encoding != schema::TextureEncoding::rgba8)
    {
        return false;
    }
    const auto size = static_cast<std::uint32_t>(data->data.size());
    basist::basisu_transcoder decoder;
    if (data->encoding == schema::TextureEncoding::basis &&
        (!decoder.validate_header(data->data.data(), size) ||
         decoder.get_total_images(data->data.data(), size) != 1 ||
         decoder.get_total_image_levels(data->data.data(), size, 0) != data->mip_count))
    {
        return false;
    }
    using Transcode = basist::transcoder_texture_format;
    const auto supported = [&caps, data, &decoder, size](Format::Enum format)
    {
        const auto required =
            data->srgb ? BGFX_CAPS_FORMAT_TEXTURE_2D_SRGB : BGFX_CAPS_FORMAT_TEXTURE_2D;
        if ((caps.formats[format] & required) == 0)
        {
            return false;
        }
        if (data->encoding != schema::TextureEncoding::basis)
        {
            return format == Format::RGBA8;
        }
        const auto target = format == Format::BC7       ? Transcode::cTFBC7_RGBA
                            : format == Format::ASTC4x4 ? Transcode::cTFASTC_4x4_RGBA
                            : format == Format::ETC2A   ? Transcode::cTFETC2_RGBA
                            : format == Format::RGBA16F ? Transcode::cTFRGBA_HALF
                                                       : Transcode::cTFRGBA32;
        return basist::basis_is_format_supported(
            target, decoder.get_basis_tex_format(data->data.data(), size));
    };
    auto requested = data->format;
    if (data->encoding == schema::TextureEncoding::rgba8)
    {
        if (requested != schema::TextureFormat::auto_format &&
            requested != schema::TextureFormat::rgba8)
        {
            return false;
        }
        requested = schema::TextureFormat::rgba8;
    }
    if (requested == schema::TextureFormat::auto_format)
    {
        requested = supported(Format::BC7)       ? schema::TextureFormat::bc7
                    : supported(Format::ASTC4x4) ? schema::TextureFormat::astc4x4
                    : supported(Format::ETC2A)   ? schema::TextureFormat::etc2
                                                 : schema::TextureFormat::rgba8;
    }
    switch (requested)
    {
    case schema::TextureFormat::bc7:
        candidate.format = Format::BC7;
        break;
    case schema::TextureFormat::astc4x4:
        candidate.format = Format::ASTC4x4;
        break;
    case schema::TextureFormat::etc2:
        candidate.format = Format::ETC2A;
        break;
    case schema::TextureFormat::rgba8:
        break;
    case schema::TextureFormat::rgba16f:
        if (data->srgb)
        {
            return false;
        }
        candidate.format = Format::RGBA16F;
        break;
    default:
        return false;
    }
    if (!supported(candidate.format))
    {
        return false;
    }
    candidate.flags = data->srgb ? BGFX_TEXTURE_SRGB : BGFX_TEXTURE_NONE;
    std::uint32_t width = data->width;
    std::uint32_t height = data->height;
    std::uint64_t total_bytes = 0;
    for (std::uint32_t mip = 0; mip < data->mip_count; ++mip)
    {
        const bool raw = candidate.format == Format::RGBA8 || candidate.format == Format::RGBA16F;
        const std::uint64_t units =
            raw ? static_cast<std::uint64_t>(width) * height
                : static_cast<std::uint64_t>((width + 3) / 4) * ((height + 3) / 4);
        const std::uint64_t level_bytes =
            units * (candidate.format == Format::RGBA16F ? 8 : raw ? 4 : 16);
        total_bytes += level_bytes;
        if (level_bytes > std::numeric_limits<std::uint32_t>::max() || total_bytes > max_bytes)
        {
            return false;
        }
        width = std::max(1U, width >> 1);
        height = std::max(1U, height >> 1);
    }
    if (data->encoding == schema::TextureEncoding::rgba8 && total_bytes != data->data.size())
    {
        return false;
    }
    candidate.bytes = static_cast<std::size_t>(total_bytes);
    *this = std::move(candidate);
    return true;
}

bool PreparedTexture::prepare()
{
    ENGINE_PROFILE_ZONE("Render.texturePrepare");
    levels.clear();
    const auto* data = resource ? resource->texture() : nullptr;
    if (data == nullptr || bytes == 0)
    {
        return false;
    }
    if (data->encoding == schema::TextureEncoding::ktx)
    {
        return true;
    }
    const auto size = static_cast<std::uint32_t>(data->data.size());
    basist::basisu_transcoder decoder;
    if (data->encoding == schema::TextureEncoding::basis &&
        !decoder.start_transcoding(data->data.data(), size))
    {
        return false;
    }
    using Transcode = basist::transcoder_texture_format;
    const auto transcode = format == bgfx::TextureFormat::BC7       ? Transcode::cTFBC7_RGBA
                           : format == bgfx::TextureFormat::ASTC4x4 ? Transcode::cTFASTC_4x4_RGBA
                           : format == bgfx::TextureFormat::ETC2A   ? Transcode::cTFETC2_RGBA
                           : format == bgfx::TextureFormat::RGBA16F ? Transcode::cTFRGBA_HALF
                                                                   : Transcode::cTFRGBA32;
    std::vector<std::vector<std::uint8_t>> candidate;
    std::uint32_t width = data->width;
    std::uint32_t height = data->height;
    std::size_t raw_offset = 0;
    for (std::uint32_t mip = 0; mip < data->mip_count; ++mip)
    {
        const bool raw = format == bgfx::TextureFormat::RGBA8 ||
                         format == bgfx::TextureFormat::RGBA16F;
        const std::size_t units =
            raw ? static_cast<std::size_t>(width) * height
                : static_cast<std::size_t>((width + 3) / 4) * ((height + 3) / 4);
        candidate.emplace_back(units * (format == bgfx::TextureFormat::RGBA16F ? 8 : raw ? 4 : 16));
        auto& output = candidate.back();
        if (data->encoding == schema::TextureEncoding::basis)
        {
            basist::basisu_image_level_info info{};
            if (!decoder.get_image_level_info(data->data.data(), size, info, 0, mip) ||
                info.m_orig_width != width || info.m_orig_height != height ||
                !decoder.transcode_image_level(data->data.data(), size, 0, mip, output.data(),
                                               static_cast<std::uint32_t>(units), transcode))
            {
                return false;
            }
        }
        else
        {
            std::copy_n(data->data.data() + raw_offset, output.size(), output.data());
            raw_offset += output.size();
        }
        width = std::max(1U, width >> 1);
        height = std::max(1U, height >> 1);
    }
    levels = std::move(candidate);
    return true;
}

bool GpuTexture::create(const PreparedTexture& prepared)
{
    ENGINE_PROFILE_ZONE("Render.textureUpload");
    const auto* data = prepared.resource ? prepared.resource->texture() : nullptr;
    if (data == nullptr || !bgfx::isTextureValid(0, data->cube_map, 1, prepared.format,
                                                prepared.flags))
    {
        return false;
    }
    if (data->encoding == schema::TextureEncoding::ktx)
    {
        bgfx::TextureInfo info{};
        handle = bgfx::createTexture(
            bgfx::copy(data->data.data(), static_cast<std::uint32_t>(data->data.size())),
            prepared.flags, 0, &info);
        if (!bgfx::isValid(handle) || info.width != data->width || info.height != data->height ||
            info.cubeMap != data->cube_map || info.numLayers != 1 ||
            info.numMips != data->mip_count || info.format != prepared.format)
        {
            release();
            return false;
        }
    }
    else
    {
        if (prepared.levels.size() != data->mip_count)
        {
            return false;
        }
        handle = bgfx::createTexture2D(static_cast<std::uint16_t>(data->width),
                                       static_cast<std::uint16_t>(data->height),
                                       data->mip_count > 1, 1, prepared.format, prepared.flags);
        if (!bgfx::isValid(handle))
        {
            return false;
        }
        std::uint32_t width = data->width;
        std::uint32_t height = data->height;
        for (std::uint32_t mip = 0; mip < data->mip_count; ++mip)
        {
            const auto& bytes = prepared.levels[mip];
            const auto* memory =
                bgfx::copy(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
            bgfx::updateTexture2D(handle, 0, static_cast<std::uint8_t>(mip), 0, 0,
                                  static_cast<std::uint16_t>(width),
                                  static_cast<std::uint16_t>(height), memory);
            width = std::max(1U, width >> 1);
            height = std::max(1U, height >> 1);
        }
    }
    resource = prepared.resource;
    return true;
}

void GpuTexture::release() noexcept
{
    if (bgfx::isValid(handle))
    {
        bgfx::destroy(handle);
    }
    handle = BGFX_INVALID_HANDLE;
    resource.reset();
}

} // namespace engine::render_detail
