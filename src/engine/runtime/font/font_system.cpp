#include "runtime/font/font_system.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/text_component.hpp"
#include "runtime/scene/transform.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace engine
{
namespace
{

std::uint32_t next_codepoint(std::string_view text, std::size_t& offset) noexcept
{
    const auto first = static_cast<unsigned char>(text[offset++]);
    if (first < 0x80)
    {
        return first;
    }
    const unsigned count = first >= 0xc2 && first <= 0xdf   ? 1
                           : first >= 0xe0 && first <= 0xef ? 2
                           : first >= 0xf0 && first <= 0xf4 ? 3
                                                            : 0;
    if (count == 0 || count > text.size() - offset)
    {
        return 0xfffd;
    }
    std::uint32_t value = first & (0x7fU >> (count + 1));
    for (unsigned index = 0; index < count; ++index)
    {
        const auto byte = static_cast<unsigned char>(text[offset + index]);
        if ((byte & 0xc0) != 0x80)
        {
            return 0xfffd;
        }
        value = (value << 6) | (byte & 0x3f);
    }
    offset += count;
    const std::uint32_t minimum[] = {0, 0x80, 0x800, 0x10000};
    return value < minimum[count] || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)
               ? 0xfffd
               : value;
}

bool load_resource(std::string_view path_or_guid, ResourcePtr& output)
{
    output.reset();
    auto* resources = ResourceSystem::instance();
    if (resources == nullptr)
    {
        return false;
    }
    std::string guid(path_or_guid);
    if (!GuidMap::valid_guid(path_or_guid) && !ResourceSystem::path_to_guid(path_or_guid, guid))
    {
        return false;
    }
    return resources->loader().load(guid, output) && output != nullptr;
}

bool finite_vertex(const FontVertex& vertex) noexcept
{
    for (float value : vertex.position)
    {
        if (!std::isfinite(value))
        {
            return false;
        }
    }
    return std::all_of(std::begin(vertex.dilation), std::end(vertex.dilation),
                       [](float value) { return std::isfinite(value); }) &&
           std::isfinite(vertex.curve_coordinate[0]) && std::isfinite(vertex.curve_coordinate[1]) &&
           std::isfinite(vertex.curve_range[0]) && std::isfinite(vertex.curve_range[1]);
}

} // namespace

struct FontSystem::GpuState
{
    struct Texture
    {
        std::weak_ptr<const Resource> resource;
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        std::uint16_t width = 0;
        std::uint16_t height = 0;

        ~Texture() noexcept
        {
            if (bgfx::isValid(handle))
            {
                bgfx::destroy(handle);
            }
        }
    };

    struct Program
    {
        ResourcePtr resource;
        bgfx::ProgramHandle handle = BGFX_INVALID_HANDLE;

        ~Program() noexcept
        {
            if (bgfx::isValid(handle))
            {
                bgfx::destroy(handle);
            }
        }
    };

    bgfx::VertexLayout layout;
    bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle texture_info = BGFX_INVALID_HANDLE;
    std::vector<std::unique_ptr<Texture>> textures;
    std::vector<std::unique_ptr<Program>> programs;

    ~GpuState() noexcept
    {
        textures.clear();
        programs.clear();
        if (bgfx::isValid(sampler))
        {
            bgfx::destroy(sampler);
        }
        if (bgfx::isValid(texture_info))
        {
            bgfx::destroy(texture_info);
        }
    }

    bool initialize()
    {
        layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord2, 4, bgfx::AttribType::Float)
            .end();
        sampler = bgfx::createUniform("s_font_curves", bgfx::UniformType::Sampler);
        texture_info = bgfx::createUniform("u_font_texture", bgfx::UniformType::Vec4);
        return layout.getStride() == sizeof(FontVertex) && bgfx::isValid(sampler) &&
               bgfx::isValid(texture_info);
    }

    Texture* get_texture(const ResourcePtr& resource)
    {
        std::erase_if(textures, [](const auto& texture) { return texture->resource.expired(); });
        for (const auto& texture : textures)
        {
            const auto existing = texture->resource.lock();
            if (existing && existing->info().guid == resource->info().guid &&
                existing->generation() == resource->generation())
            {
                texture->resource = resource;
                return texture.get();
            }
        }
        const auto& curves = resource->font()->curves;
        const auto* caps = bgfx::getCaps();
        if (caps == nullptr ||
            (caps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_2D) == 0 ||
            curves.size() > (1U << 22))
        {
            return nullptr;
        }
        const std::size_t texels = std::max<std::size_t>(2, curves.size() * 2);
        const auto maximum = std::min<std::uint32_t>(caps->limits.maxTextureSize, 2048);
        if (maximum < 2)
        {
            return nullptr;
        }
        const auto width = static_cast<std::uint16_t>(std::min<std::size_t>(texels, maximum));
        const auto height = (texels + width - 1) / width;
        if (height > caps->limits.maxTextureSize || height > 65535 ||
            height * width > std::numeric_limits<std::uint32_t>::max() / (4 * sizeof(float)))
        {
            return nullptr;
        }
        std::vector<float> pixels(static_cast<std::size_t>(width) * height * 4, 0.0F);
        for (std::size_t index = 0; index < curves.size(); ++index)
        {
            const auto& curve = curves[index];
            float* target = pixels.data() + index * 8;
            target[0] = curve.x0();
            target[1] = curve.y0();
            target[2] = curve.x1();
            target[3] = curve.y1();
            target[4] = curve.x2();
            target[5] = curve.y2();
        }
        auto texture = std::make_unique<Texture>();
        texture->width = width;
        texture->height = static_cast<std::uint16_t>(height);
        constexpr std::uint64_t flags = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
                                        BGFX_SAMPLER_MIP_POINT | BGFX_SAMPLER_U_CLAMP |
                                        BGFX_SAMPLER_V_CLAMP;
        texture->handle = bgfx::createTexture2D(
            width, texture->height, false, 1, bgfx::TextureFormat::RGBA32F, flags,
            bgfx::copy(pixels.data(), static_cast<std::uint32_t>(pixels.size() * sizeof(float))));
        if (!bgfx::isValid(texture->handle))
        {
            return nullptr;
        }
        texture->resource = resource;
        Texture* result = texture.get();
        textures.push_back(std::move(texture));
        return result;
    }

    Program* get_program(const ResourcePtr& resource)
    {
        for (const auto& program : programs)
        {
            if (program->resource->info().guid == resource->info().guid &&
                program->resource->generation() == resource->generation())
            {
                return program.get();
            }
        }
        const auto& shader = *resource->shader();
        if (shader.vertex_code.empty() || shader.fragment_code.empty() ||
            shader.vertex_code.size() > std::numeric_limits<std::uint32_t>::max() ||
            shader.fragment_code.size() > std::numeric_limits<std::uint32_t>::max())
        {
            return nullptr;
        }
        const auto vertex = bgfx::createShader(bgfx::copy(
            shader.vertex_code.data(), static_cast<std::uint32_t>(shader.vertex_code.size())));
        const auto fragment = bgfx::createShader(bgfx::copy(
            shader.fragment_code.data(), static_cast<std::uint32_t>(shader.fragment_code.size())));
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
            return nullptr;
        }
        bool has_curves = false;
        bool has_texture_info = false;
        bool compatible = true;
        for (auto stage : {vertex, fragment})
        {
            const auto count = bgfx::getShaderUniforms(stage);
            std::vector<bgfx::UniformHandle> uniforms(count);
            bgfx::getShaderUniforms(stage, uniforms.data(), count);
            for (auto uniform : uniforms)
            {
                bgfx::UniformInfo info{};
                bgfx::getUniformInfo(uniform, info);
                const std::string_view name(info.name);
                if (name == "s_font_curves")
                {
                    has_curves = true;
                    compatible =
                        compatible && info.type == bgfx::UniformType::Sampler && info.num == 1;
                }
                else if (name == "u_font_texture")
                {
                    has_texture_info = true;
                    compatible =
                        compatible && info.type == bgfx::UniformType::Vec4 && info.num == 1;
                }
                else
                {
                    compatible = false;
                }
            }
        }
        if (!compatible || !has_curves || !has_texture_info)
        {
            bgfx::destroy(vertex);
            bgfx::destroy(fragment);
            return nullptr;
        }
        auto program = std::make_unique<Program>();
        program->handle = bgfx::createProgram(vertex, fragment);
        bgfx::destroy(vertex);
        bgfx::destroy(fragment);
        if (!bgfx::isValid(program->handle))
        {
            return nullptr;
        }
        program->resource = resource;
        Program* result = program.get();
        programs.push_back(std::move(program));
        return result;
    }
};

FontSystem::FontSystem() = default;

FontSystem::~FontSystem() noexcept
{
    shutdown();
}

bool FontSystem::on_initialize(const EngineConfig&)
{
    last_error_.clear();
    return true;
}

void FontSystem::on_shutdown() noexcept
{
    release_scene();
}

bool FontSystem::load_font(std::string_view path_or_guid, ResourcePtr& output)
{
    output.reset();
    if (!is_initialized() || !load_resource(path_or_guid, output) || output->font() == nullptr)
    {
        output.reset();
        last_error_ = "Cannot load font resource: " + std::string(path_or_guid);
        return false;
    }
    last_error_.clear();
    return true;
}

const schema::FontGlyphT* FontSystem::glyph(const schema::FontDataT& data,
                                            std::uint32_t codepoint) noexcept
{
    const auto item = std::lower_bound(data.glyphs.begin(), data.glyphs.end(), codepoint,
                                       [](const auto& glyph, std::uint32_t value)
                                       { return glyph->codepoint < value; });
    if (item != data.glyphs.end() && (*item)->codepoint == codepoint)
    {
        return item->get();
    }
    return data.glyphs.empty() ? nullptr : data.glyphs.front().get();
}

float FontSystem::kerning(const schema::FontDataT& data, std::uint32_t left,
                          std::uint32_t right) noexcept
{
    const auto key = std::pair{left, right};
    const auto item = std::lower_bound(data.kerning.begin(), data.kerning.end(), key,
                                       [](const auto& value, const auto& target)
                                       { return std::pair{value.left(), value.right()} < target; });
    return item != data.kerning.end() && item->left() == left && item->right() == right
               ? item->advance()
               : 0.0F;
}

float FontSystem::measure(const schema::FontDataT& data, std::string_view text, float size,
                          float letter_spacing) noexcept
{
    if (!std::isfinite(size) || size <= 0.0F || !std::isfinite(letter_spacing))
    {
        return 0.0F;
    }
    float width = 0.0F;
    float maximum = 0.0F;
    std::uint32_t previous = 0;
    bool first = true;
    for (std::size_t offset = 0; offset < text.size();)
    {
        const auto codepoint = next_codepoint(text, offset);
        if (codepoint == '\r')
        {
            continue;
        }
        if (codepoint == '\n')
        {
            maximum = std::max(maximum, width);
            width = 0;
            previous = 0;
            first = true;
            continue;
        }
        const auto* current = glyph(data, codepoint == '\t' ? ' ' : codepoint);
        if (current == nullptr)
        {
            continue;
        }
        if (!first)
        {
            width += letter_spacing + kerning(data, previous, current->codepoint) * size;
        }
        width += current->advance_x * size * (codepoint == '\t' ? 4.0F : 1.0F);
        previous = current->codepoint;
        first = false;
    }
    return std::max(maximum, width);
}

bool FontSystem::append_glyph(const schema::FontDataT& data, const schema::FontGlyphT& glyph,
                              float x, float y, float size, std::uint32_t color, bool y_down,
                              FontGeometry& output)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(size) || size <= 0.0F ||
        glyph.curve_offset > data.curves.size() ||
        glyph.curve_count > data.curves.size() - glyph.curve_offset ||
        output.vertices.size() > std::numeric_limits<std::uint32_t>::max() - 4)
    {
        return false;
    }
    if (glyph.curve_count == 0)
    {
        return true;
    }
    const float padding = std::max(0.05F, y_down ? 1.0F / size : 0.05F);
    const float bounds[4][2] = {{glyph.min_x - padding, glyph.min_y - padding},
                                {glyph.max_x + padding, glyph.min_y - padding},
                                {glyph.max_x + padding, glyph.max_y + padding},
                                {glyph.min_x - padding, glyph.max_y + padding}};
    FontVertex vertices[4]{};
    for (std::size_t index = 0; index < 4; ++index)
    {
        auto& vertex = vertices[index];
        vertex.position[0] = x + bounds[index][0] * size;
        vertex.position[1] = y + bounds[index][1] * (y_down ? -size : size);
        vertex.color = color;
        vertex.curve_coordinate[0] = bounds[index][0];
        vertex.curve_coordinate[1] = bounds[index][1];
        vertex.curve_range[0] = static_cast<float>(glyph.curve_offset);
        vertex.curve_range[1] = static_cast<float>(glyph.curve_count);
        vertex.dilation[0] = index == 0 || index == 3 ? -1.0F : 1.0F;
        vertex.dilation[1] = (index < 2 ? -1.0F : 1.0F) * (y_down ? -1.0F : 1.0F);
        vertex.dilation[2] = 1.0F / size;
        vertex.dilation[3] = (y_down ? -1.0F : 1.0F) / size;
        if (!finite_vertex(vertex))
        {
            return false;
        }
    }
    const auto base = static_cast<std::uint32_t>(output.vertices.size());
    output.vertices.insert(output.vertices.end(), std::begin(vertices), std::end(vertices));
    const std::uint32_t indices[] = {base, base + 1, base + 2, base, base + 2, base + 3};
    output.indices.insert(output.indices.end(), std::begin(indices), std::end(indices));
    return true;
}

bool FontSystem::layout(const Resource& font, std::string_view text, const FontLayout& settings,
                        FontGeometry& output)
{
    output = {};
    const auto* data = font.font();
    if (data == nullptr || !std::isfinite(settings.size) || settings.size <= 0.0F ||
        !std::isfinite(settings.letter_spacing))
    {
        return false;
    }
    FontGeometry result;
    float x = 0.0F;
    float y = 0.0F;
    const float line_height = (data->ascender - data->descender + data->line_gap) * settings.size;
    std::uint32_t previous = 0;
    bool first = true;
    result.height = line_height;
    for (std::size_t offset = 0; offset < text.size();)
    {
        const auto codepoint = next_codepoint(text, offset);
        if (codepoint == '\r')
        {
            continue;
        }
        if (codepoint == '\n')
        {
            result.width = std::max(result.width, x);
            result.height += line_height;
            x = 0.0F;
            y += line_height * (settings.y_down ? 1.0F : -1.0F);
            previous = 0;
            first = true;
            continue;
        }
        const auto* current = glyph(*data, codepoint == '\t' ? ' ' : codepoint);
        if (current == nullptr)
        {
            return false;
        }
        if (!first)
        {
            x += settings.letter_spacing +
                 kerning(*data, previous, current->codepoint) * settings.size;
        }
        if (!append_glyph(*data, *current, x, y, settings.size, settings.color, settings.y_down,
                          result))
        {
            return false;
        }
        x += current->advance_x * settings.size * (codepoint == '\t' ? 4.0F : 1.0F);
        previous = current->codepoint;
        first = false;
    }
    result.width = std::max(result.width, x);
    output = std::move(result);
    return true;
}

bool FontSystem::draw(const ResourcePtr& font, const FontGeometry& geometry,
                      const FontDraw& settings)
{
    if (!is_initialized() || !font || font->font() == nullptr || bgfx::getCaps() == nullptr)
    {
        last_error_ = "Font rendering requires an initialized font system and renderer";
        return false;
    }
    if (geometry.indices.empty())
    {
        return true;
    }
    if (geometry.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        geometry.indices.size() > std::numeric_limits<std::uint32_t>::max() ||
        geometry.indices.size() % 3 != 0 ||
        !std::all_of(geometry.vertices.begin(), geometry.vertices.end(),
                     [&font](const FontVertex& vertex)
                     {
                         const double first = vertex.curve_range[0];
                         const double count = vertex.curve_range[1];
                         return finite_vertex(vertex) && first >= 0.0 && count >= 0.0 &&
                                std::floor(first) == first && std::floor(count) == count &&
                                first + count <= static_cast<double>(font->font()->curves.size());
                     }) ||
        !std::all_of(settings.transform.begin(), settings.transform.end(),
                     [](float value) { return std::isfinite(value); }) ||
        std::any_of(geometry.indices.begin(), geometry.indices.end(),
                    [&geometry](auto index) { return index >= geometry.vertices.size(); }))
    {
        last_error_ = "Invalid font draw geometry or transform";
        return false;
    }
    if (!gpu_)
    {
        auto state = std::make_unique<GpuState>();
        if (!state->initialize())
        {
            last_error_ = "Cannot create font GPU uniforms";
            return false;
        }
        gpu_ = std::move(state);
    }
    const auto& shader = settings.shader;
    if (!shader || shader->shader() == nullptr)
    {
        last_error_ = "Font drawing requires a loaded Shader resource";
        return false;
    }
    auto* texture = gpu_->get_texture(font);
    if (texture == nullptr)
    {
        last_error_ = "Cannot create RGBA32F font curve texture within device limits";
        return false;
    }
    auto* program = gpu_->get_program(shader);
    if (program == nullptr)
    {
        last_error_ = "Cannot create compatible font shader (s_font_curves and u_font_texture): " +
                      shader->info().path;
        return false;
    }
    bgfx::TransientVertexBuffer vertices{};
    bgfx::TransientIndexBuffer indices{};
    if (!bgfx::allocTransientBuffers(&vertices, gpu_->layout,
                                     static_cast<std::uint32_t>(geometry.vertices.size()), &indices,
                                     static_cast<std::uint32_t>(geometry.indices.size()), true))
    {
        last_error_ = "Font geometry exceeds the available transient GPU buffer";
        return false;
    }
    std::memcpy(vertices.data, geometry.vertices.data(),
                geometry.vertices.size() * sizeof(FontVertex));
    std::memcpy(indices.data, geometry.indices.data(),
                geometry.indices.size() * sizeof(std::uint32_t));
    bgfx::setVertexBuffer(0, &vertices);
    bgfx::setIndexBuffer(&indices);
    bgfx::setTransform(settings.transform.data());
    if (settings.use_scissor)
    {
        bgfx::setScissor(settings.scissor[0], settings.scissor[1], settings.scissor[2],
                         settings.scissor[3]);
    }
    else
    {
        bgfx::setScissor(UINT16_MAX);
    }
    const float texture_info[] = {static_cast<float>(texture->width),
                                  static_cast<float>(texture->height), 0.0F, 0.0F};
    bgfx::setUniform(gpu_->texture_info, texture_info);
    bgfx::setTexture(0, gpu_->sampler, texture->handle);
    std::uint64_t state =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    if (settings.depth_test)
    {
        state |= BGFX_STATE_DEPTH_TEST_LEQUAL;
    }
    bgfx::setState(state);
    bgfx::submit(settings.view_id, program->handle);
    last_error_.clear();
    return true;
}

bool FontSystem::render_scene(const Scene& scene, std::uint16_t view_id)
{
    for (const auto& node : scene.nodes())
    {
        const auto* text = node->get_component<TextComponent>();
        if (text == nullptr || !text->enabled() || text->settings().text.empty())
        {
            continue;
        }
        const auto* transform = node->get_component<Transform>();
        FontDraw draw_settings;
        draw_settings.view_id = view_id;
        draw_settings.depth_test = true;
        draw_settings.shader = text->shader();
        if (transform == nullptr || !transform->world_matrix(draw_settings.transform.data()))
        {
            last_error_ = "Invalid text node transform";
            return false;
        }
        const auto& color = text->settings().color;
        const auto channel = [](float value)
        { return static_cast<std::uint32_t>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F); };
        FontLayout layout_settings;
        layout_settings.size = text->settings().size;
        layout_settings.letter_spacing = text->settings().letter_spacing;
        layout_settings.color = channel(color.r() * color.a()) |
                                (channel(color.g() * color.a()) << 8) |
                                (channel(color.b() * color.a()) << 16) | (channel(color.a()) << 24);
        layout_settings.y_down = false;
        FontGeometry geometry;
        if (!text->font() ||
            !layout(*text->font(), text->settings().text, layout_settings, geometry) ||
            !draw(text->font(), geometry, draw_settings))
        {
            if (last_error_.empty())
            {
                last_error_ = "Cannot build text node geometry";
            }
            return false;
        }
    }
    return true;
}

void FontSystem::release_scene() noexcept
{
    gpu_.reset();
}

std::uint32_t FontSystem::gpu_resource_count() const noexcept
{
    return gpu_ ? static_cast<std::uint32_t>(gpu_->textures.size() + gpu_->programs.size() + 2) : 0;
}

const std::string& FontSystem::last_error() const noexcept
{
    return last_error_;
}

} // namespace engine
