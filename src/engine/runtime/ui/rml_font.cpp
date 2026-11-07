#include "runtime/ui/rml_font.hpp"
#include "runtime/resource/types/shader_resource.hpp"
#include "runtime/resource/types/texture_resource.hpp"
#include "runtime/resource/types/ui_resource.hpp"
#include "runtime/scene/resource_binding.hpp"
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
bool RmlFont::activate(const std::string& key, const resource_detail::ResourceReference& document)
{
    active_ = nullptr;
    if (!document || !document.data<UiResource>())
    {
        return false;
    }
    auto found = documents_.find(key);
    if (found == documents_.end())
    {
        Bindings bindings;
        auto* resources = document.loader();
        if (!resources)
        {
            return false;
        }
        for (const auto& reference : document.data<UiResource>()->fonts)
        {
            Binding binding;
            Guid shader_guid;
            if (!reference ||
                !binding.font.dependency(resources, document.handle(), reference->font_guid) ||
                !binding.font.data<FontResource>() ||
                !ecs_detail::acquire_dependency(resources, document.handle(),
                                                reference->shader_path, binding.shader) ||
                !binding.shader || !binding.shader.data<ShaderResource>() ||
                binding.font->generation() != document->generation() ||
                binding.shader->generation() != document->generation() ||
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
void RmlFont::prune(std::span<const std::string> keys)
{
    const auto alive = [&](const std::string& key)
    { return std::find(keys.begin(), keys.end(), key) != keys.end(); };
    if (!alive(active_key_))
    {
        active_ = nullptr;
        active_key_.clear();
    }
    std::erase_if(faces_, [&](const auto& entry) { return !alive(entry.second->document_key); });
    std::erase_if(documents_, [&](const auto& entry) { return !alive(entry.first); });
}
bool RmlFont::resolve_texture(std::string_view key, std::string_view font_guid,
                              std::string_view shader_guid,
                              resource_detail::ResourceReference& font,
                              resource_detail::ResourceReference& shader) const
{
    font.reset();
    shader.reset();
    const auto document = documents_.find(std::string(key));
    if (document == documents_.end())
    {
        return false;
    }
    for (const auto& [family, binding] : document->second)
    {
        if (binding.font->info().guid == font_guid && binding.shader->info().guid == shader_guid)
        {
            if (!font.retain(binding.font) || !shader.retain(binding.shader))
            {
                font.reset();
                shader.reset();
                return false;
            }
            return true;
        }
    }
    return false;
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
        if (!face->resource.retain(found->second.font))
        {
            face.reset();
            return 0;
        }
        face->document_key = active_key_;
        face->shader_guid = found->second.shader->info().guid;
        const auto& data = *face->resource.data<FontResource>();
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
    float width = FontSystem::measure(
        *face.resource.data<FontResource>(), std::string_view(text.begin(), text.size()),
        static_cast<float>(face.metrics.size), context.letter_spacing);
    if (!text.empty())
    {
        width += context.letter_spacing;
    }
    if (!text.empty() && prior != Rml::Character::Null)
    {
        width += FontSystem::kerning(*face.resource.data<FontResource>(),
                                     static_cast<std::uint32_t>(prior),
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
    const auto& data = *face.resource.data<FontResource>();
    Rml::TexturedMesh mesh;
    mesh.texture =
        manager.LoadTexture("engine-font/" + face.document_key + "/" + face.resource->info().guid +
                            "/" + face.shader_guid + "/" + std::to_string(face.metrics.size));
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
