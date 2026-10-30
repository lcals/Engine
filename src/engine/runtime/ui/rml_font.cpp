#include "runtime/ui/rml_font.hpp"
#include "runtime/resource/resource_system.hpp"
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace engine::ui_detail
{
namespace
{
std::string normalized(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
                   { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c); });
    return value;
}
} // namespace
RmlFont::RmlFont(FontSystem& fonts) noexcept : fonts_(fonts)
{
}
bool RmlFont::activate(const ResourcePtr& document)
{
    active_ = nullptr;
    if (!document || !document->ui())
    {
        return false;
    }
    const auto key = document->info().guid + ":" + std::to_string(document->generation());
    auto found = documents_.find(key);
    if (found == documents_.end())
    {
        Bindings bindings;
        auto* resources = ResourceSystem::instance();
        if (!resources)
        {
            return false;
        }
        for (const auto& reference : document->ui()->fonts)
        {
            Binding binding;
            Guid shader_guid;
            if (!reference || !fonts_.load_font(reference->font_guid, binding.font) ||
                !ResourceSystem::path_to_guid(reference->shader_path, shader_guid) ||
                !resources->loader().load(shader_guid, binding.shader) || !binding.shader ||
                !binding.shader->shader() ||
                !bindings.emplace(normalized(reference->family), std::move(binding)).second)
            {
                return false;
            }
        }
        found = documents_.emplace(key, std::move(bindings)).first;
    }
    active_key_ = key;
    active_ = &found->second;
    return true;
}
Rml::FontFaceHandle RmlFont::GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle,
                                               Rml::Style::FontWeight, int size)
{
    if (!active_)
    {
        return 0;
    }
    const auto found = active_->find(normalized(family));
    if (found == active_->end() || size <= 0 || size > 4096)
    {
        return 0;
    }
    const auto key = std::make_pair(active_key_ + ":" + found->first, size);
    auto& face = faces_[key];
    if (!face)
    {
        face = std::make_unique<Face>();
        face->resource = found->second.font;
        face->shader_guid = found->second.shader->info().guid;
        const auto& data = *face->resource->font();
        const float scale = static_cast<float>(size);
        auto& m = face->metrics;
        m.size = size;
        m.ascent = data.ascender * scale;
        m.descent = -data.descender * scale;
        m.line_spacing = (data.ascender - data.descender + data.line_gap) * scale;
        const auto* x = FontSystem::glyph(data, 'x');
        m.x_height = x ? x->max_y * scale : scale * 0.5F;
        m.underline_position = -data.underline_position * scale;
        m.underline_thickness = std::max(1.0F, data.underline_thickness * scale);
    }
    return reinterpret_cast<Rml::FontFaceHandle>(face.get());
}
const Rml::FontMetrics& RmlFont::GetFontMetrics(Rml::FontFaceHandle handle)
{
    static const Rml::FontMetrics empty{};
    return handle ? reinterpret_cast<Face*>(handle)->metrics : empty;
}
int RmlFont::GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView text,
                            const Rml::TextShapingContext& context, Rml::Character prior)
{
    if (!handle)
    {
        return 0;
    }
    const auto& face = *reinterpret_cast<Face*>(handle);
    float width =
        FontSystem::measure(*face.resource->font(), std::string_view(text.begin(), text.size()),
                            static_cast<float>(face.metrics.size), context.letter_spacing);
    if (!text.empty())
    {
        width += context.letter_spacing;
    }
    if (!text.empty() && prior != Rml::Character::Null)
    {
        width += FontSystem::kerning(*face.resource->font(), static_cast<std::uint32_t>(prior),
                                     static_cast<std::uint32_t>(Rml::StringUtilities::ToCharacter(
                                         text.begin(), text.begin() + text.size()))) *
                 face.metrics.size;
    }
    return static_cast<int>(std::round(width));
}
int RmlFont::GenerateString(Rml::RenderManager& manager, Rml::FontFaceHandle handle,
                            Rml::FontEffectsHandle, Rml::StringView text, Rml::Vector2f position,
                            Rml::ColourbPremultiplied colour, float,
                            const Rml::TextShapingContext& context, Rml::TexturedMeshList& meshes)
{
    if (!handle || text.empty())
    {
        return 0;
    }
    const auto& face = *reinterpret_cast<Face*>(handle);
    const auto& data = *face.resource->font();
    Rml::TexturedMesh mesh;
    mesh.texture = manager.LoadTexture("engine-font/" + face.resource->info().guid + "/" +
                                       face.shader_guid + "/" + std::to_string(face.metrics.size));
    float x = 0.0F;
    std::uint32_t previous = 0;
    std::uint32_t packed = 0;
    std::memcpy(&packed, &colour, sizeof(packed));
    for (Rml::StringIteratorU8 it(text); it; ++it)
    {
        const auto codepoint = static_cast<std::uint32_t>(*it);
        const auto* glyph = FontSystem::glyph(data, codepoint);
        if (!glyph)
        {
            continue;
        }
        x += FontSystem::kerning(data, previous, glyph->codepoint) * face.metrics.size;
        if (glyph->curve_count != 0)
        {
            FontGeometry quad;
            if (!FontSystem::append_glyph(data, *glyph, position.x + x, position.y,
                                          static_cast<float>(face.metrics.size), packed, true,
                                          quad))
            {
                return 0;
            }
            const auto index = static_cast<int>(mesh.mesh.vertices.size());
            const auto found =
                std::lower_bound(data.glyphs.begin(), data.glyphs.end(), glyph->codepoint,
                                 [](const auto& candidate, std::uint32_t value)
                                 { return candidate->codepoint < value; });
            const auto glyph_index = static_cast<float>(found - data.glyphs.begin());
            for (std::size_t corner = 0; corner < quad.vertices.size(); ++corner)
            {
                const auto& vertex = quad.vertices[corner];
                mesh.mesh.vertices.push_back({{vertex.position[0], vertex.position[1]},
                                              colour,
                                              {glyph_index, static_cast<float>(corner)}});
            }
            for (auto vertex : quad.indices)
            {
                mesh.mesh.indices.push_back(index + static_cast<int>(vertex));
            }
        }
        x += glyph->advance_x * face.metrics.size + context.letter_spacing;
        previous = glyph->codepoint;
    }
    if (!mesh.mesh.indices.empty())
    {
        meshes.push_back(std::move(mesh));
    }
    return static_cast<int>(std::round(x));
}
void RmlFont::ReleaseFontResources()
{
    faces_.clear();
    documents_.clear();
    active_ = nullptr;
    active_key_.clear();
}
} // namespace engine::ui_detail
