#include "runtime/ui/rml_render.hpp"

#include "fs_ui.bin.h"
#include "runtime/resource/resource_system.hpp"
#include "vs_ui.bin.h"
#include <RmlUi/Core/Rectangle.h>
#include <algorithm>
#include <basisu/transcoder/basisu_transcoder.h>
#include <bx/math.h>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>

namespace engine::ui_detail
{
namespace
{
constexpr bgfx::ViewId ui_view = 200;
constexpr auto sampler_flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
} // namespace
RmlRender::RmlRender(FontSystem& fonts) noexcept : fonts_(fonts)
{
    bx::mtxIdentity(transform_.data());
}
RmlRender::~RmlRender()
{
    release();
}
bool RmlRender::begin(std::uint32_t width, std::uint32_t height, int logical_width,
                      int logical_height)
{
    if (!healthy_)
    {
        return false;
    }
    if (width == 0 || height == 0 || width > 65535 || height > 65535 || logical_width <= 0 ||
        logical_height <= 0)
    {
        return false;
    }
    if (!bgfx::isValid(program_))
    {
        const auto vertex = bgfx::createShader(bgfx::copy(vs_ui, sizeof(vs_ui)));
        const auto fragment = bgfx::createShader(bgfx::copy(fs_ui, sizeof(fs_ui)));
        if (!bgfx::isValid(vertex) || !bgfx::isValid(fragment))
        {
            if (bgfx::isValid(vertex))
            {
                bgfx::destroy(vertex);
            }
            if (bgfx::isValid(fragment))
            {
                bgfx::destroy(fragment);
            }
            return false;
        }
        program_ = bgfx::createProgram(vertex, fragment, true);
        sampler_ = bgfx::createUniform("s_ui_texture", bgfx::UniformType::Sampler);
        const std::uint32_t white = 0xffffffff;
        white_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, sampler_flags,
                                       bgfx::copy(&white, sizeof(white)));
        layout_.begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        if (!bgfx::isValid(program_) || !bgfx::isValid(sampler_) || !bgfx::isValid(white_))
        {
            release();
            return false;
        }
    }
    width_ = width;
    height_ = height;
    scale_x_ = static_cast<float>(width) / logical_width;
    scale_y_ = static_cast<float>(height) / logical_height;
    float projection[16];
    bx::mtxOrtho(projection, 0.0F, static_cast<float>(logical_width),
                 static_cast<float>(logical_height), 0.0F, 0.0F, 1000.0F, 0.0F,
                 bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewName(ui_view, "Scene UI");
    bgfx::setViewMode(ui_view, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(ui_view, 0, 0, static_cast<std::uint16_t>(width),
                      static_cast<std::uint16_t>(height));
    bgfx::setViewClear(ui_view, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(ui_view, nullptr, projection);
    scissor_enabled_ = false;
    bx::mtxIdentity(transform_.data());
    return true;
}
void RmlRender::set_document(const schema::UiDocumentDataT& data)
{
    document_ = &data;
    allowed_textures_ = data.texture_guids;
}
bool RmlRender::healthy() const noexcept
{
    return healthy_;
}
Rml::CompiledGeometryHandle RmlRender::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                       Rml::Span<const int> indices)
{
    if (vertices.empty() || indices.empty() || vertices.size() > 1000000 ||
        indices.size() > 3000000)
    {
        healthy_ = false;
        return 0;
    }
    Geometry geometry;
    geometry.vertices.assign(vertices.begin(), vertices.end());
    for (int index : indices)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= vertices.size())
        {
            healthy_ = false;
            return 0;
        }
        geometry.indices.push_back(static_cast<std::uint32_t>(index));
    }
    const auto handle = next_geometry_++;
    geometries_.emplace(handle, std::move(geometry));
    return handle;
}
void RmlRender::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                               Rml::TextureHandle texture)
{
    const auto found = geometries_.find(handle);
    if (found == geometries_.end())
    {
        healthy_ = false;
        return;
    }
    if (scissor_enabled_ && (scissor_[2] == 0 || scissor_[3] == 0))
    {
        return;
    }
    const Texture* image = nullptr;
    if (texture != 0)
    {
        const auto entry = textures_.find(texture);
        if (entry == textures_.end())
        {
            healthy_ = false;
            return;
        }
        image = &entry->second;
        if (image->resource)
        {
            const bool font_allowed =
                image->font_size > 0 && document_ && image->shader &&
                std::any_of(document_->fonts.begin(), document_->fonts.end(),
                            [&](const auto& binding)
                            {
                                return binding &&
                                       binding->font_guid == image->resource->info().guid &&
                                       binding->shader_path == image->shader->info().path;
                            });
            const bool texture_allowed =
                image->font_size == 0 &&
                std::find(allowed_textures_.begin(), allowed_textures_.end(),
                          image->resource->info().guid) != allowed_textures_.end();
            if (!font_allowed && !texture_allowed)
            {
                healthy_ = false;
                return;
            }
        }
    }
    const auto& mesh = found->second;
    float offset[16];
    bx::mtxTranslate(offset, translation.x, translation.y, 0.0F);
    std::array<float, 16> matrix{};
    bx::mtxMul(matrix.data(), offset, transform_.data());
    if (image && image->font_size > 0 && image->resource && image->resource->font())
    {
        FontGeometry geometry;
        geometry.indices = mesh.indices;
        geometry.vertices.reserve(mesh.vertices.size());
        const auto& font = *image->resource->font();
        for (const auto& vertex : mesh.vertices)
        {
            if (!std::isfinite(vertex.tex_coord.x) || !std::isfinite(vertex.tex_coord.y) ||
                vertex.tex_coord.x < 0 || vertex.tex_coord.x >= font.glyphs.size() ||
                vertex.tex_coord.y < 0 || vertex.tex_coord.y > 3)
            {
                healthy_ = false;
                return;
            }
            FontGeometry quad;
            std::uint32_t color;
            std::memcpy(&color, &vertex.colour, sizeof(color));
            if (!FontSystem::append_glyph(
                    font, *font.glyphs[static_cast<std::size_t>(vertex.tex_coord.x)], 0, 0,
                    image->font_size, color, true, quad))
            {
                healthy_ = false;
                return;
            }
            if (quad.vertices.size() != 4)
            {
                healthy_ = false;
                return;
            }
            auto value = quad.vertices[static_cast<std::size_t>(vertex.tex_coord.y)];
            value.position[0] = vertex.position.x;
            value.position[1] = vertex.position.y;
            geometry.vertices.push_back(value);
        }
        FontDraw draw;
        draw.view_id = ui_view;
        draw.transform = matrix;
        draw.use_scissor = scissor_enabled_;
        draw.scissor = scissor_;
        draw.shader = image->shader;
        healthy_ = fonts_.draw(image->resource, geometry, draw) && healthy_;
        return;
    }
    const auto vertex_count = static_cast<std::uint32_t>(mesh.vertices.size());
    const auto index_count = static_cast<std::uint32_t>(mesh.indices.size());
    if (bgfx::getAvailTransientVertexBuffer(vertex_count, layout_) < vertex_count ||
        bgfx::getAvailTransientIndexBuffer(index_count, true) < index_count)
    {
        healthy_ = false;
        return;
    }
    bgfx::TransientVertexBuffer vertices;
    bgfx::TransientIndexBuffer indices;
    bgfx::allocTransientVertexBuffer(&vertices, vertex_count, layout_);
    bgfx::allocTransientIndexBuffer(&indices, index_count, true);
    static_assert(sizeof(Rml::Vertex) == 20);
    std::memcpy(vertices.data, mesh.vertices.data(), mesh.vertices.size() * sizeof(Rml::Vertex));
    std::memcpy(indices.data, mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t));
    bgfx::setVertexBuffer(0, &vertices);
    bgfx::setIndexBuffer(&indices);
    bgfx::setTransform(matrix.data());
    bgfx::setTexture(0, sampler_, image ? image->gpu : white_);
    if (scissor_enabled_)
    {
        bgfx::setScissor(scissor_[0], scissor_[1], scissor_[2], scissor_[3]);
    }
    else
    {
        bgfx::setScissor(UINT16_MAX);
    }
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
                   BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::submit(ui_view, program_);
}
void RmlRender::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
    geometries_.erase(geometry);
}
Rml::TextureHandle RmlRender::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source)
{
    dimensions = {};
    if (source.starts_with("engine-font/"))
    {
        const auto split = source.rfind('/');
        if (split <= 12)
        {
            healthy_ = false;
            return 0;
        }
        int size = 0;
        const auto result =
            std::from_chars(source.data() + split + 1, source.data() + source.size(), size);
        if (result.ec != std::errc{} || result.ptr != source.data() + source.size() || size <= 0 ||
            size > 4096)
        {
            healthy_ = false;
            return 0;
        }
        const auto shader_split = source.rfind('/', split - 1);
        if (shader_split <= 12)
        {
            healthy_ = false;
            return 0;
        }
        const auto font_guid = std::string_view(source).substr(12, shader_split - 12);
        const auto shader_guid =
            std::string_view(source).substr(shader_split + 1, split - shader_split - 1);
        Texture texture;
        auto* resources = ResourceSystem::instance();
        if (!resources || !fonts_.load_font(font_guid, texture.resource) ||
            !resources->loader().load(shader_guid, texture.shader) || !texture.shader ||
            !texture.shader->shader() || !document_ ||
            !std::any_of(document_->fonts.begin(), document_->fonts.end(),
                         [&](const auto& binding)
                         {
                             return binding && binding->font_guid == font_guid &&
                                    binding->shader_path == texture.shader->info().path;
                         }))
        {
            healthy_ = false;
            return 0;
        }
        texture.font_size = static_cast<float>(size);
        const auto handle = next_texture_++;
        textures_.emplace(handle, std::move(texture));
        dimensions = {1, 1};
        return handle;
    }
    auto* resources = ResourceSystem::instance();
    Guid guid;
    ResourcePtr resource;
    if (GuidMap::valid_guid(source))
    {
        guid = source;
    }
    if (!resources || (guid.empty() && !ResourceSystem::path_to_guid(source, guid)) ||
        !resources->loader().load(guid, resource) || !resource || !resource->texture())
    {
        healthy_ = false;
        return 0;
    }
    if (std::find(allowed_textures_.begin(), allowed_textures_.end(), guid) ==
        allowed_textures_.end())
    {
        healthy_ = false;
        return 0;
    }
    const auto& data = *resource->texture();
    if (data.width == 0 || data.height == 0 || data.width > 8192 || data.height > 8192)
    {
        healthy_ = false;
        return 0;
    }
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(data.width) * data.height * 4);
    if (data.encoding == schema::TextureEncoding::rgba8)
    {
        if (data.data.size() < pixels.size())
        {
            healthy_ = false;
            return 0;
        }
        std::copy_n(data.data.begin(), pixels.size(), pixels.begin());
    }
    else
    {
        basist::basisu_transcoder_init();
        basist::basisu_transcoder decoder;
        const auto size = static_cast<std::uint32_t>(data.data.size());
        if (!decoder.start_transcoding(data.data.data(), size) ||
            !decoder.transcode_image_level(data.data.data(), size, 0, 0, pixels.data(),
                                           data.width * data.height,
                                           basist::transcoder_texture_format::cTFRGBA32))
        {
            healthy_ = false;
            return 0;
        }
    }
    for (std::size_t i = 0; i < pixels.size(); i += 4)
    {
        for (std::size_t c = 0; c < 3; ++c)
        {
            pixels[i + c] = static_cast<std::uint8_t>(
                (static_cast<unsigned int>(pixels[i + c]) * pixels[i + 3] + 127) / 255);
        }
    }
    dimensions = {static_cast<int>(data.width), static_cast<int>(data.height)};
    const auto handle =
        GenerateTexture(Rml::Span<const Rml::byte>(pixels.data(), pixels.size()), dimensions);
    if (handle)
    {
        textures_[handle].resource = std::move(resource);
    }
    return handle;
}
Rml::TextureHandle RmlRender::GenerateTexture(Rml::Span<const Rml::byte> source,
                                              Rml::Vector2i dimensions)
{
    if (dimensions.x <= 0 || dimensions.y <= 0 || dimensions.x > 8192 || dimensions.y > 8192 ||
        source.size() != static_cast<std::size_t>(dimensions.x) * dimensions.y * 4)
    {
        healthy_ = false;
        return 0;
    }
    Texture texture;
    texture.gpu = bgfx::createTexture2D(
        static_cast<std::uint16_t>(dimensions.x), static_cast<std::uint16_t>(dimensions.y), false,
        1, bgfx::TextureFormat::RGBA8, sampler_flags,
        bgfx::copy(source.data(), static_cast<std::uint32_t>(source.size())));
    if (!bgfx::isValid(texture.gpu))
    {
        healthy_ = false;
        return 0;
    }
    const auto handle = next_texture_++;
    textures_.emplace(handle, std::move(texture));
    return handle;
}
void RmlRender::ReleaseTexture(Rml::TextureHandle texture)
{
    const auto it = textures_.find(texture);
    if (it == textures_.end())
    {
        return;
    }
    if (bgfx::isValid(it->second.gpu))
    {
        bgfx::destroy(it->second.gpu);
    }
    textures_.erase(it);
}
void RmlRender::EnableScissorRegion(bool enabled)
{
    scissor_enabled_ = enabled;
}
void RmlRender::SetScissorRegion(Rml::Rectanglei region)
{
    const auto x = std::clamp(static_cast<int>(std::floor(region.Left() * scale_x_)), 0,
                              static_cast<int>(width_));
    const auto y = std::clamp(static_cast<int>(std::floor(region.Top() * scale_y_)), 0,
                              static_cast<int>(height_));
    const auto right = std::clamp(static_cast<int>(std::ceil(region.Right() * scale_x_)), x,
                                  static_cast<int>(width_));
    const auto bottom = std::clamp(static_cast<int>(std::ceil(region.Bottom() * scale_y_)), y,
                                   static_cast<int>(height_));
    scissor_ = {static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y),
                static_cast<std::uint16_t>(right - x), static_cast<std::uint16_t>(bottom - y)};
}
void RmlRender::SetTransform(const Rml::Matrix4f* matrix)
{
    if (matrix)
    {
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                transform_[static_cast<std::size_t>(column * 4 + row)] =
                    matrix->GetColumn(column)[row];
            }
        }
    }
    else
    {
        bx::mtxIdentity(transform_.data());
    }
}
void RmlRender::release() noexcept
{
    healthy_ = true;
    for (const auto& [id, texture] : textures_)
    {
        static_cast<void>(id);
        if (bgfx::isValid(texture.gpu))
        {
            bgfx::destroy(texture.gpu);
        }
    }
    textures_.clear();
    geometries_.clear();
    document_ = nullptr;
    allowed_textures_ = {};
    if (bgfx::isValid(white_))
    {
        bgfx::destroy(white_);
    }
    if (bgfx::isValid(sampler_))
    {
        bgfx::destroy(sampler_);
    }
    if (bgfx::isValid(program_))
    {
        bgfx::destroy(program_);
    }
    white_ = BGFX_INVALID_HANDLE;
    sampler_ = BGFX_INVALID_HANDLE;
    program_ = BGFX_INVALID_HANDLE;
}
} // namespace engine::ui_detail
