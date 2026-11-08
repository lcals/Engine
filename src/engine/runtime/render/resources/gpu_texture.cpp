#include "runtime/render/resources/gpu_texture.hpp"
#include "runtime/render/texture_upload.hpp"

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
      handle(std::exchange(other.handle, bgfx::TextureHandle{bgfx::kInvalidHandle})),
      sampler_flags(std::exchange(other.sampler_flags, 0))
{
}

GpuTexture& GpuTexture::operator=(GpuTexture&& other) noexcept
{
    if (this != &other)
    {
        release();
        resource = std::move(other.resource);
        handle = std::exchange(other.handle, bgfx::TextureHandle{bgfx::kInvalidHandle});
        sampler_flags = std::exchange(other.sampler_flags, 0);
    }
    return *this;
}

namespace
{
bool block_format(bgfx::TextureFormat::Enum format) noexcept
{
    return format == bgfx::TextureFormat::BC7 || format == bgfx::TextureFormat::ASTC4x4 ||
           format == bgfx::TextureFormat::ETC2A;
}
std::size_t unit_bytes(bgfx::TextureFormat::Enum format) noexcept
{
    return block_format(format) ? 16 : format == bgfx::TextureFormat::RGBA16F ? 8 : 4;
}
basist::transcoder_texture_format transcode_format(bgfx::TextureFormat::Enum format)
{
    using Target = basist::transcoder_texture_format;
    switch (format)
    {
    case bgfx::TextureFormat::BC7:
        return Target::cTFBC7_RGBA;
    case bgfx::TextureFormat::ASTC4x4:
        return Target::cTFASTC_4x4_RGBA;
    case bgfx::TextureFormat::ETC2A:
        return Target::cTFETC2_RGBA;
    case bgfx::TextureFormat::RGBA16F:
        return Target::cTFRGBA_HALF;
    default:
        return Target::cTFRGBA32;
    }
}
} // namespace

std::size_t texture_region_bytes(const PreparedTexture& part, std::uint64_t offset,
                                 std::size_t maximum) noexcept
{
    if (!part.data || part.mip >= part.data->mip_count)
    {
        return 0;
    }
    const auto block = block_format(part.format) ? 4U : 1U;
    const auto width = std::max(1U, part.data->width >> part.mip);
    const auto height = std::max(1U, part.data->height >> part.mip);
    const auto unit = unit_bytes(part.format);
    const std::size_t row = ((width + block - 1) / block) * unit;
    const std::size_t total = row * ((height + block - 1) / block);
    if (offset >= total || offset % unit != 0)
    {
        return 0;
    }
    const auto available = std::min<std::size_t>(maximum, total - offset);
    const auto column = static_cast<std::size_t>(offset % row);
    if (column != 0 || available < row)
    {
        return std::min(available, row - column) / unit * unit;
    }
    return available / row * row;
}

bool PreparedTexture::describe(ResourceLoader* loader, ResourceHandle source,
                               const bgfx::Caps& caps, std::size_t max_bytes)
{
    PreparedTexture candidate;
    candidate.mip = mip;
    candidate.face = face;
    candidate.byte_offset = byte_offset;
    if (!candidate.resource.retain(loader, source))
    {
        return false;
    }
    candidate.data = candidate.resource.data<TextureResource>();
    const auto* data = candidate.data;
    if (!data || data->width == 0 || data->height == 0 || mip >= data->mip_count ||
        face >= (data->cube_map ? 6U : 1U) || data->width > caps.limits.maxTextureSize ||
        data->height > caps.limits.maxTextureSize || data->width > UINT16_MAX ||
        data->height > UINT16_MAX || data->data.size() > UINT32_MAX)
    {
        return false;
    }
    const bool basis = data->encoding == schema::TextureEncoding::basis;
    if (!basis && data->encoding != schema::TextureEncoding::native)
    {
        return false;
    }
    using Format = bgfx::TextureFormat;
    basist::basisu_transcoder decoder;
    const auto encoded_size = static_cast<std::uint32_t>(data->data.size());
    const auto supported = [&](Format::Enum format)
    {
        const auto required =
            data->cube_map
                ? (data->srgb ? BGFX_CAPS_FORMAT_TEXTURE_CUBE_SRGB : BGFX_CAPS_FORMAT_TEXTURE_CUBE)
                : (data->srgb ? BGFX_CAPS_FORMAT_TEXTURE_2D_SRGB : BGFX_CAPS_FORMAT_TEXTURE_2D);
        return (caps.formats[format] & required) != 0 &&
               (!basis || basist::basis_is_format_supported(
                              transcode_format(format),
                              decoder.get_basis_tex_format(data->data.data(), encoded_size)));
    };
    auto requested = data->format;
    if (requested == schema::TextureFormat::auto_format)
    {
        if (!basis)
        {
            return false;
        }
        requested = supported(Format::BC7)       ? schema::TextureFormat::bc7
                    : supported(Format::ASTC4x4) ? schema::TextureFormat::astc4x4
                    : supported(Format::ETC2A)   ? schema::TextureFormat::etc2
                                                 : schema::TextureFormat::rgba8;
    }
    switch (requested)
    {
    case schema::TextureFormat::rgba8:
        candidate.format = Format::RGBA8;
        break;
    case schema::TextureFormat::bc7:
        candidate.format = Format::BC7;
        break;
    case schema::TextureFormat::astc4x4:
        candidate.format = Format::ASTC4x4;
        break;
    case schema::TextureFormat::etc2:
        candidate.format = Format::ETC2A;
        break;
    case schema::TextureFormat::rgba16f:
        candidate.format = Format::RGBA16F;
        break;
    case schema::TextureFormat::rg16f:
        candidate.format = Format::RG16F;
        break;
    default:
        return false;
    }
    if (!supported(candidate.format))
    {
        return false;
    }
    candidate.flags = data->srgb ? BGFX_TEXTURE_SRGB : BGFX_TEXTURE_NONE;
    if (data->cube_map || candidate.format == Format::RGBA16F || candidate.format == Format::RG16F)
    {
        candidate.flags |= BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
    }
    if (basis)
    {
        if (face != 0 || byte_offset != 0)
        {
            return false;
        }
        candidate.bytes =
            texture_region_bytes(candidate, 0, std::numeric_limits<std::size_t>::max());
        if (candidate.bytes > max_bytes)
        {
            return false;
        }
    }
    else
    {
        candidate.bytes = texture_region_bytes(candidate, byte_offset, max_bytes);
    }
    if (candidate.bytes == 0 || candidate.bytes > UINT32_MAX)
    {
        return false;
    }
    *this = std::move(candidate);
    return true;
}

bool PreparedTexture::prepare()
{
    ENGINE_PROFILE_ZONE("Render.texturePrepare");
    pixels.clear();
    view = {};
    if (!data || bytes == 0)
    {
        return false;
    }
    pixels.resize(bytes);
    if (data->encoding == schema::TextureEncoding::native)
    {
        const auto found = std::find_if(data->subresources.begin(), data->subresources.end(),
                                        [this](const auto& part)
                                        { return part.mip() == mip && part.face() == face; });
        if (found == data->subresources.end() || byte_offset > found->size() ||
            bytes > found->size() - byte_offset || found->offset() > data->data.size() ||
            found->size() > data->data.size() - found->offset())
        {
            pixels.clear();
            return false;
        }
        std::copy_n(data->data.data() + found->offset() + byte_offset, bytes, pixels.data());
    }
    else if (data->encoding == schema::TextureEncoding::basis)
    {
        basist::basisu_transcoder decoder;
        const auto size = static_cast<std::uint32_t>(data->data.size());
        if (!decoder.start_transcoding(data->data.data(), size) ||
            !decoder.transcode_image_level(data->data.data(), size, 0, mip, pixels.data(),
                                           static_cast<std::uint32_t>(bytes / unit_bytes(format)),
                                           transcode_format(format)))
        {
            pixels.clear();
            return false;
        }
    }
    else
    {
        pixels.clear();
        return false;
    }
    view = pixels;
    return true;
}

bool GpuTexture::create(const PreparedTexture& prepared)
{
    release();
    const auto* data = prepared.data;
    if (!data || !bgfx::isTextureValid(0, data->cube_map, 1, prepared.format, prepared.flags) ||
        !resource.retain(prepared.resource))
    {
        return false;
    }
    handle = data->cube_map
                 ? bgfx::createTextureCube(static_cast<std::uint16_t>(data->width),
                                           data->mip_count > 1, 1, prepared.format, prepared.flags)
                 : bgfx::createTexture2D(static_cast<std::uint16_t>(data->width),
                                         static_cast<std::uint16_t>(data->height),
                                         data->mip_count > 1, 1, prepared.format, prepared.flags);
    if (!bgfx::isValid(handle))
    {
        release();
        return false;
    }
    sampler_flags = static_cast<std::uint32_t>(prepared.flags & BGFX_SAMPLER_BITS_MASK);
    return true;
}

bool GpuTexture::upload(const PreparedTexture& part)
{
    ENGINE_PROFILE_ZONE("Render.textureUpload");
    if (!bgfx::isValid(handle) || !part.data || resource.data<TextureResource>() != part.data ||
        part.face >= (part.data->cube_map ? 6U : 1U) || part.bytes == 0 ||
        part.view.size() != part.bytes || part.bytes > UINT32_MAX ||
        texture_region_bytes(part, part.byte_offset, part.bytes) != part.bytes)
    {
        return false;
    }
    const auto block = block_format(part.format) ? 4U : 1U;
    const auto width = std::max(1U, part.data->width >> part.mip);
    const auto height = std::max(1U, part.data->height >> part.mip);
    const auto unit = unit_bytes(part.format);
    const auto columns = (width + block - 1) / block;
    const auto start = part.byte_offset / unit;
    const auto x = static_cast<std::uint16_t>((start % columns) * block);
    const auto y = static_cast<std::uint16_t>((start / columns) * block);
    const auto units = part.bytes / unit;
    const auto upload_width = static_cast<std::uint16_t>(
        std::min<std::uint64_t>(width - x, std::min<std::size_t>(units, columns) * block));
    const auto upload_height = static_cast<std::uint16_t>(
        std::min<std::size_t>(height - y, std::max<std::size_t>(1, units / columns) * block));
    const auto* memory = bgfx::copy(part.view.data(), static_cast<std::uint32_t>(part.bytes));
    if (part.data->cube_map)
    {
        bgfx::updateTextureCube(handle, 0, static_cast<std::uint8_t>(part.face),
                                static_cast<std::uint8_t>(part.mip), x, y, upload_width,
                                upload_height, memory);
    }
    else
    {
        bgfx::updateTexture2D(handle, 0, static_cast<std::uint8_t>(part.mip), x, y, upload_width,
                              upload_height, memory);
    }
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
    sampler_flags = 0;
}

} // namespace engine::render_detail

namespace engine::render
{
bool upload_texture(ResourceLoader& loader, ResourceHandle resource, bgfx::TextureHandle& output)
{
    if (bgfx::isValid(output))
    {
        return false;
    }
    const auto* caps = bgfx::getCaps();
    const TextureResource* source = nullptr;
    if (!caps || loader.get(resource, source) != ResourceError::none)
    {
        return false;
    }
    render_detail::GpuTexture candidate;
    const auto& data = source->data();
    for (std::uint32_t mip = 0; mip < data.mip_count; ++mip)
    {
        for (std::uint32_t face = 0; face < (data.cube_map ? 6U : 1U); ++face)
        {
            render_detail::PreparedTexture prepared;
            prepared.mip = mip;
            prepared.face = face;
            if (!prepared.describe(&loader, resource, *caps,
                                   std::numeric_limits<std::size_t>::max()) ||
                !prepared.prepare() ||
                (!bgfx::isValid(candidate.handle) && !candidate.create(prepared)) ||
                !candidate.upload(prepared))
            {
                return false;
            }
        }
    }
    output = std::exchange(candidate.handle, bgfx::TextureHandle{bgfx::kInvalidHandle});
    return true;
}
} // namespace engine::render
