#include "runtime/resource/codecs/font_resource_codec.hpp"

#include <flatbuffers/verifier.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

namespace engine
{
namespace
{
constexpr std::size_t maximum_glyphs = 4096;
constexpr std::size_t maximum_curves = 1024U * 1024U;
constexpr std::size_t maximum_kerning = 1024U * 1024U;

bool scalar(std::uint32_t codepoint) noexcept
{
    return codepoint <= 0x10ffff && (codepoint < 0xd800 || codepoint > 0xdfff);
}

bool metric(float value) noexcept
{
    return std::isfinite(value) && std::abs(value) <= 128.0F;
}
} // namespace

bool FontResourceCodec::validate(const schema::FontDataT& data) noexcept
{
    if (data.schema_version != 1 || data.family.empty() || data.family.size() > 256 ||
        data.style.empty() || data.style.size() > 256 || data.units_per_em < 16 ||
        data.units_per_em > 16384 || data.glyphs.empty() || data.glyphs.size() > maximum_glyphs ||
        data.curves.size() > maximum_curves || data.kerning.size() > maximum_kerning ||
        !metric(data.ascender) || !metric(data.descender) || !metric(data.line_gap) ||
        !metric(data.underline_position) || !metric(data.underline_thickness) ||
        data.ascender <= data.descender || data.underline_thickness < 0.0F ||
        data.ascender - data.descender + data.line_gap <= 0.0F)
    {
        return false;
    }
    std::uint32_t previous = 0;
    std::size_t next_curve = 0;
    for (std::size_t index = 0; index < data.glyphs.size(); ++index)
    {
        const auto& glyph = data.glyphs[index];
        if (!glyph || !scalar(glyph->codepoint) ||
            (index == 0 ? glyph->codepoint != 0 || glyph->glyph_index != 0
                        : glyph->codepoint <= previous || glyph->glyph_index == 0) ||
            !metric(glyph->advance_x) || !metric(glyph->min_x) || !metric(glyph->min_y) ||
            !metric(glyph->max_x) || !metric(glyph->max_y) || glyph->min_x > glyph->max_x ||
            glyph->min_y > glyph->max_y || glyph->curve_offset != next_curve ||
            glyph->curve_count > 4096 || next_curve > data.curves.size() ||
            glyph->curve_count > data.curves.size() - next_curve)
        {
            return false;
        }
        next_curve += glyph->curve_count;
        previous = glyph->codepoint;
    }
    if (next_curve != data.curves.size())
    {
        return false;
    }
    for (const auto& curve : data.curves)
    {
        const std::array coordinates = {curve.x0(), curve.y0(), curve.x1(),
                                        curve.y1(), curve.x2(), curve.y2()};
        if (!std::all_of(coordinates.begin(), coordinates.end(), metric))
        {
            return false;
        }
    }
    const auto has_glyph = [&data](std::uint32_t codepoint)
    {
        const auto found = std::lower_bound(data.glyphs.begin(), data.glyphs.end(), codepoint,
                                            [](const auto& glyph, std::uint32_t value)
                                            { return glyph->codepoint < value; });
        return found != data.glyphs.end() && (*found)->codepoint == codepoint;
    };
    std::uint64_t previous_pair = 0;
    for (std::size_t index = 0; index < data.kerning.size(); ++index)
    {
        const auto& pair = data.kerning[index];
        const auto key = (static_cast<std::uint64_t>(pair.left()) << 32) | pair.right();
        if ((index > 0 && key <= previous_pair) || !metric(pair.advance()) ||
            pair.advance() == 0.0F || !has_glyph(pair.left()) || !has_glyph(pair.right()))
        {
            return false;
        }
        previous_pair = key;
    }
    return true;
}

bool FontResourceCodec::decode(const ResourceCatalog&, ByteView payload, Resource& output) const
{
    if (payload.empty() || payload.size() > 64U * 1024U * 1024U ||
        !output.info().dependencies.empty())
    {
        return false;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
    flatbuffers::Verifier verifier(bytes, payload.size());
    if (!schema::VerifyFontDataBuffer(verifier))
    {
        return false;
    }
    const auto* encoded = schema::GetFontData(bytes);
    if (encoded->glyphs()->size() > maximum_glyphs || encoded->curves()->size() > maximum_curves ||
        (encoded->kerning() != nullptr && encoded->kerning()->size() > maximum_kerning))
    {
        return false;
    }
    auto data = schema::UnPackFontData(bytes);
    if (!validate(*data))
    {
        return false;
    }
    store(output, std::move(data));
    return true;
}

} // namespace engine
