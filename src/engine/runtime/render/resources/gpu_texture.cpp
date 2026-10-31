#include "runtime/render/resources/gpu_texture.hpp"

#include "runtime/resource/resource.hpp"

#include <SDL3/SDL.h>
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

bool GpuTexture::create(const ResourcePtr& source)
{
    const auto* data = source ? source->texture() : nullptr;
    const auto* caps = bgfx::getCaps();
    if (data == nullptr || data->width == 0 || data->height == 0 ||
        data->width > caps->limits.maxTextureSize || data->height > caps->limits.maxTextureSize ||
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
    const auto size = static_cast<std::uint32_t>(data->data.size());
    if (data->encoding == schema::TextureEncoding::ktx)
    {
        const auto format = data->format == schema::TextureFormat::rgba16f
                                ? bgfx::TextureFormat::RGBA16F
                                : bgfx::TextureFormat::RG16F;
        const auto required =
            data->cube_map ? BGFX_CAPS_FORMAT_TEXTURE_CUBE : BGFX_CAPS_FORMAT_TEXTURE_2D;
        if (data->srgb ||
            (data->format != schema::TextureFormat::rgba16f &&
             data->format != schema::TextureFormat::rg16f) ||
            (caps->formats[format] & required) == 0 ||
            !bgfx::isTextureValid(0, data->cube_map, 1, format, 0))
        {
            return false;
        }
        bgfx::TextureInfo info{};
        handle = bgfx::createTexture(
            bgfx::copy(data->data.data(), size),
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP, 0, &info);
        if (!bgfx::isValid(handle) || info.width != data->width || info.height != data->height ||
            info.cubeMap != data->cube_map || info.numLayers != 1 ||
            info.numMips != data->mip_count || info.format != format)
        {
            if (bgfx::isValid(handle))
            {
                bgfx::destroy(handle);
                handle = BGFX_INVALID_HANDLE;
            }
            return false;
        }
        resource = source;
        return true;
    }
    basist::basisu_transcoder decoder;
    if (data->encoding == schema::TextureEncoding::basis &&
        (!decoder.validate_header(data->data.data(), size) ||
         decoder.get_total_images(data->data.data(), size) != 1 ||
         decoder.get_total_image_levels(data->data.data(), size, 0) != data->mip_count ||
         !decoder.start_transcoding(data->data.data(), size)))
    {
        return false;
    }
    if (data->encoding != schema::TextureEncoding::basis &&
        data->encoding != schema::TextureEncoding::rgba8)
    {
        return false;
    }
    using Format = bgfx::TextureFormat;
    using Transcode = basist::transcoder_texture_format;
    Format::Enum format = Format::RGBA8;
    Transcode transcode = Transcode::cTFRGBA32;
    const auto supported = [caps, data, &decoder, size](Format::Enum candidate)
    {
        const auto required =
            data->srgb ? BGFX_CAPS_FORMAT_TEXTURE_2D_SRGB : BGFX_CAPS_FORMAT_TEXTURE_2D;
        if ((caps->formats[candidate] & required) == 0)
        {
            return false;
        }
        if (data->encoding != schema::TextureEncoding::basis)
        {
            return candidate == Format::RGBA8;
        }
        const auto target = candidate == Format::BC7       ? Transcode::cTFBC7_RGBA
                            : candidate == Format::ASTC4x4 ? Transcode::cTFASTC_4x4_RGBA
                            : candidate == Format::ETC2A   ? Transcode::cTFETC2_RGBA
                                                           : Transcode::cTFRGBA32;
        return basist::basis_is_format_supported(target,
                                                 decoder.get_tex_format(data->data.data(), size));
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
        format = Format::BC7;
        transcode = Transcode::cTFBC7_RGBA;
        break;
    case schema::TextureFormat::astc4x4:
        format = Format::ASTC4x4;
        transcode = Transcode::cTFASTC_4x4_RGBA;
        break;
    case schema::TextureFormat::etc2:
        format = Format::ETC2A;
        transcode = Transcode::cTFETC2_RGBA;
        break;
    case schema::TextureFormat::rgba8:
        break;
    default:
        return false;
    }
    const std::uint64_t flags = data->srgb ? BGFX_TEXTURE_SRGB : BGFX_TEXTURE_NONE;
    if (!supported(format) || !bgfx::isTextureValid(0, false, 1, format, flags))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unsupported GPU texture format for %s",
                     data->path.c_str());
        return false;
    }
    std::vector<std::vector<std::uint8_t>> levels;
    std::uint32_t width = data->width;
    std::uint32_t height = data->height;
    std::size_t raw_offset = 0;
    std::uint64_t total_bytes = 0;
    for (std::uint32_t mip = 0; mip < data->mip_count; ++mip)
    {
        const bool raw = format == Format::RGBA8;
        const std::uint64_t units =
            raw ? static_cast<std::uint64_t>(width) * height
                : static_cast<std::uint64_t>((width + 3) / 4) * ((height + 3) / 4);
        const std::uint64_t bytes = units * (raw ? 4 : 16);
        total_bytes += bytes;
        if (bytes > std::numeric_limits<std::uint32_t>::max() || total_bytes > 512ULL * 1024 * 1024)
        {
            return false;
        }
        levels.emplace_back(static_cast<std::size_t>(bytes));
        auto& output = levels.back();
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
            if (raw_offset > data->data.size() || output.size() > data->data.size() - raw_offset)
            {
                return false;
            }
            std::copy_n(data->data.data() + raw_offset, output.size(), output.data());
            raw_offset += output.size();
        }
        width = std::max(1U, width >> 1);
        height = std::max(1U, height >> 1);
    }
    if (data->encoding == schema::TextureEncoding::rgba8 && raw_offset != data->data.size())
    {
        return false;
    }
    handle = bgfx::createTexture2D(static_cast<std::uint16_t>(data->width),
                                   static_cast<std::uint16_t>(data->height), data->mip_count > 1, 1,
                                   format, flags);
    if (!bgfx::isValid(handle))
    {
        return false;
    }
    width = data->width;
    height = data->height;
    for (std::uint32_t mip = 0; mip < data->mip_count; ++mip)
    {
        const auto& bytes = levels[mip];
        bgfx::updateTexture2D(handle, 0, static_cast<std::uint8_t>(mip), 0, 0,
                              static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height),
                              bgfx::copy(bytes.data(), static_cast<std::uint32_t>(bytes.size())));
        width = std::max(1U, width >> 1);
        height = std::max(1U, height >> 1);
    }
    resource = source;
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
