#include "plugins/editor/font_import/font_importer.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "runtime/resource/codecs/font_resource_codec.hpp"

#include <flatbuffers/flatbuffers.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_FONT_FORMATS_H
#include FT_OUTLINE_H
#include <toml++/toml.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::plugins::font_import
{
namespace
{
constexpr std::size_t maximum_source_size = 32U * 1024U * 1024U;
constexpr std::size_t maximum_glyphs = 4096;
constexpr std::size_t maximum_codepoints = 16384;
constexpr std::size_t maximum_curves = 1024U * 1024U;
constexpr std::size_t maximum_kerning = 1024U * 1024U;

struct FontSettings
{
    std::int64_t face_index = 0;
    bool kerning = true;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges = {{0x20, 0x7e}, {0xa0, 0xff}};
};

struct FontSource
{
    FT_Library library = nullptr;
    FT_Face face = nullptr;

    ~FontSource()
    {
        if (face != nullptr)
        {
            FT_Done_Face(face);
        }
        if (library != nullptr)
        {
            FT_Done_FreeType(library);
        }
    }
};

bool parse_range(std::string_view text, std::pair<std::uint32_t, std::uint32_t>& range)
{
    const auto separator = text.find('-');
    if (separator == std::string_view::npos)
    {
        return false;
    }
    const auto left = text.substr(0, separator);
    const auto right = text.substr(separator + 1);
    const auto parse = [](std::string_view value, std::uint32_t& output)
    {
        if (value.size() < 4 || value.size() > 6)
        {
            return false;
        }
        const auto result = std::from_chars(value.data(), value.data() + value.size(), output, 16);
        return result.ec == std::errc{} && result.ptr == value.data() + value.size();
    };
    return parse(left, range.first) && parse(right, range.second) && range.first > 0 &&
           range.first <= range.second && range.second <= 0x10ffff &&
           (range.second < 0xd800 || range.first > 0xdfff);
}

bool read_settings(const std::filesystem::path& source, FontSettings& output, std::string& error)
{
    auto meta = source;
    meta += ".meta";
    toml::table root;
    if (!editor::asset_pipeline::read_table(meta, root, error))
    {
        return false;
    }
    if (!root.contains("font"))
    {
        return true;
    }
    const auto* table = root["font"].as_table();
    if (table == nullptr)
    {
        error = "font meta requires a [font] table";
        return false;
    }
    for (const auto& [key, value] : *table)
    {
        static_cast<void>(value);
        if (key != "face_index" && key != "unicode_ranges" && key != "kerning")
        {
            error = "unknown font meta setting: " + std::string(key.str());
            return false;
        }
    }
    if (table->contains("face_index"))
    {
        const auto value = (*table)["face_index"].value<std::int64_t>();
        if (!(*table)["face_index"].is_integer() || !value || *value < 0 || *value > 255)
        {
            error = "font face_index must be an integer in [0,255]";
            return false;
        }
        output.face_index = *value;
    }
    if (table->contains("kerning"))
    {
        const auto value = (*table)["kerning"].value<bool>();
        if (!value)
        {
            error = "font kerning must be a boolean";
            return false;
        }
        output.kerning = *value;
    }
    if (table->contains("unicode_ranges"))
    {
        const auto* ranges = (*table)["unicode_ranges"].as_array();
        if (ranges == nullptr || ranges->empty() || ranges->size() > 256)
        {
            error = "font unicode_ranges must contain 1..256 hexadecimal ranges";
            return false;
        }
        output.ranges.clear();
        for (const auto& item : *ranges)
        {
            const auto text = item.value<std::string>();
            std::pair<std::uint32_t, std::uint32_t> range;
            if (!text || !parse_range(*text, range))
            {
                error = "font unicode range requires XXXX-XXXX Unicode scalar bounds";
                return false;
            }
            output.ranges.push_back(range);
        }
        std::sort(output.ranges.begin(), output.ranges.end());
        std::size_t count = 0;
        std::uint32_t previous = 0;
        for (const auto& [first, last] : output.ranges)
        {
            if (first <= previous || last - first + 1U > maximum_codepoints - count)
            {
                error = "font unicode ranges must not overlap or exceed 16384 codepoints";
                return false;
            }
            count += last - first + 1U;
            previous = last;
        }
    }
    return true;
}

struct OutlineBuilder
{
    std::vector<schema::FontCurve>& curves;
    float scale;
    FT_Vector current{};
    FT_Vector start{};
    bool contour = false;
    bool cubic = false;

    int quadratic(const FT_Vector& control, const FT_Vector& end)
    {
        if (curves.size() >= maximum_curves)
        {
            return 1;
        }
        curves.emplace_back(
            static_cast<float>(current.x) * scale, static_cast<float>(current.y) * scale,
            static_cast<float>(control.x) * scale, static_cast<float>(control.y) * scale,
            static_cast<float>(end.x) * scale, static_cast<float>(end.y) * scale);
        current = end;
        return 0;
    }

    int line(const FT_Vector& end)
    {
        if (current.x == end.x && current.y == end.y)
        {
            return 0;
        }
        if (curves.size() >= maximum_curves)
        {
            return 1;
        }
        // Keep half-unit control coordinates instead of rounding an integer midpoint.
        const float x0 = static_cast<float>(current.x) * scale;
        const float y0 = static_cast<float>(current.y) * scale;
        const float x2 = static_cast<float>(end.x) * scale;
        const float y2 = static_cast<float>(end.y) * scale;
        curves.emplace_back(x0, y0, (x0 + x2) * 0.5F, (y0 + y2) * 0.5F, x2, y2);
        current = end;
        return 0;
    }

    int close()
    {
        return contour ? line(start) : 0;
    }
};

int move_to(const FT_Vector* point, void* user)
{
    auto& builder = *static_cast<OutlineBuilder*>(user);
    if (builder.close() != 0)
    {
        return 1;
    }
    builder.start = *point;
    builder.current = *point;
    builder.contour = true;
    return 0;
}

int line_to(const FT_Vector* point, void* user)
{
    return static_cast<OutlineBuilder*>(user)->line(*point);
}

int conic_to(const FT_Vector* control, const FT_Vector* point, void* user)
{
    return static_cast<OutlineBuilder*>(user)->quadratic(*control, *point);
}

int cubic_to(const FT_Vector*, const FT_Vector*, const FT_Vector*, void* user)
{
    static_cast<OutlineBuilder*>(user)->cubic = true;
    return 1;
}

bool append_glyph(FT_Face face, std::uint32_t codepoint, FT_UInt glyph_index,
                  schema::FontDataT& font, std::string& error)
{
    if (font.glyphs.size() >= maximum_glyphs)
    {
        error = "font import exceeds the 4096 glyph limit";
        return false;
    }
    if (FT_Load_Glyph(face, glyph_index,
                      FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP |
                          FT_LOAD_IGNORE_TRANSFORM) != 0 ||
        face->glyph->format != FT_GLYPH_FORMAT_OUTLINE ||
        FT_Outline_Check(&face->glyph->outline) != 0)
    {
        error = "font has an unreadable or unsupported glyph outline";
        return false;
    }
    auto glyph = std::make_unique<schema::FontGlyphT>();
    const float scale = 1.0F / static_cast<float>(font.units_per_em);
    glyph->codepoint = codepoint;
    glyph->glyph_index = glyph_index;
    glyph->advance_x = static_cast<float>(face->glyph->metrics.horiAdvance) * scale;
    glyph->curve_offset = static_cast<std::uint32_t>(font.curves.size());
    FT_BBox box{};
    FT_Outline_Get_CBox(&face->glyph->outline, &box);
    glyph->min_x = static_cast<float>(box.xMin) * scale;
    glyph->min_y = static_cast<float>(box.yMin) * scale;
    glyph->max_x = static_cast<float>(box.xMax) * scale;
    glyph->max_y = static_cast<float>(box.yMax) * scale;
    if (face->glyph->outline.n_points > 0)
    {
        OutlineBuilder builder{font.curves, scale};
        const FT_Outline_Funcs callbacks = {move_to, line_to, conic_to, cubic_to, 0, 0};
        if (FT_Outline_Decompose(&face->glyph->outline, &callbacks, &builder) != 0 ||
            builder.close() != 0)
        {
            error = builder.cubic ? "font cubic outlines are unsupported; use a TrueType font"
                                  : "font outline exceeds the curve budget or is invalid";
            return false;
        }
    }
    glyph->curve_count = static_cast<std::uint32_t>(font.curves.size()) - glyph->curve_offset;
    font.glyphs.push_back(std::move(glyph));
    return true;
}

bool convert_font(FT_Face face, const FontSettings& settings, schema::FontDataT& font,
                  std::string& error)
{
    const char* format = FT_Get_Font_Format(face);
    if (format == nullptr || std::strcmp(format, "TrueType") != 0 || !FT_IS_SCALABLE(face) ||
        face->units_per_EM < 16 || face->units_per_EM > 16384 ||
        FT_Select_Charmap(face, FT_ENCODING_UNICODE) != 0)
    {
        error = "font import requires a scalable Unicode TrueType outline font";
        return false;
    }
    font.family = face->family_name != nullptr ? face->family_name : "Unnamed";
    font.style = face->style_name != nullptr ? face->style_name : "Regular";
    font.units_per_em = face->units_per_EM;
    const float scale = 1.0F / static_cast<float>(font.units_per_em);
    font.ascender = static_cast<float>(face->ascender) * scale;
    font.descender = static_cast<float>(face->descender) * scale;
    font.line_gap = static_cast<float>(face->height - face->ascender + face->descender) * scale;
    font.underline_position = static_cast<float>(face->underline_position) * scale;
    font.underline_thickness = static_cast<float>(face->underline_thickness) * scale;
    if (!append_glyph(face, 0, 0, font, error))
    {
        return false;
    }
    for (const auto& [first, last] : settings.ranges)
    {
        for (auto codepoint = first; codepoint <= last; ++codepoint)
        {
            const auto glyph = FT_Get_Char_Index(face, codepoint);
            if (glyph != 0 && !append_glyph(face, codepoint, glyph, font, error))
            {
                return false;
            }
        }
    }
    if (font.glyphs.size() == 1)
    {
        error = "font unicode ranges contain no supported characters";
        return false;
    }
    if (settings.kerning && FT_HAS_KERNING(face))
    {
        for (const auto& left : font.glyphs)
        {
            for (const auto& right : font.glyphs)
            {
                FT_Vector delta{};
                if (FT_Get_Kerning(face, left->glyph_index, right->glyph_index, FT_KERNING_UNSCALED,
                                   &delta) != 0)
                {
                    error = "font kerning table could not be read";
                    return false;
                }
                if (delta.x != 0)
                {
                    if (font.kerning.size() >= maximum_kerning)
                    {
                        error = "font kerning exceeds the pair budget";
                        return false;
                    }
                    font.kerning.emplace_back(left->codepoint, right->codepoint,
                                              static_cast<float>(delta.x) * scale);
                }
            }
        }
    }
    return true;
}
} // namespace

std::string_view FontImporter::id() const noexcept
{
    return "font";
}

schema::AssetKind FontImporter::kind() const noexcept
{
    return schema::AssetKind::font;
}

bool FontImporter::matches(const std::filesystem::path& source) const
{
    const auto extension = editor::asset_pipeline::source_extension(source);
    return extension == ".ttf" || extension == ".ttc" || extension == ".otf";
}

std::string_view FontImporter::default_meta() const noexcept
{
    return "\n[font]\nface_index = 0\nunicode_ranges = [\"0020-007E\", \"00A0-00FF\"]\n"
           "kerning = true\n";
}

bool FontImporter::inspect_batch(const editor::asset_pipeline::ImportContext&,
                                 std::string& fingerprint, std::string& error)
{
    if (!editor::asset_pipeline::schema_fingerprint({"font.fbs"}, fingerprint, error))
    {
        return false;
    }
    FontSource source;
    if (FT_Init_FreeType(&source.library) != 0)
    {
        error = "font importer could not initialize FreeType";
        return false;
    }
    FT_Int major = 0;
    FT_Int minor = 0;
    FT_Int patch = 0;
    FT_Library_Version(source.library, &major, &minor, &patch);
    fingerprint += ":font-importer-1:freetype:" + std::to_string(major) + "." +
                   std::to_string(minor) + "." + std::to_string(patch);
    return true;
}

bool FontImporter::convert(const editor::asset_pipeline::ImportAsset& asset,
                           const editor::asset_pipeline::ImportContext& context,
                           std::string& error) const
{
    FontSettings settings;
    std::vector<std::byte> bytes;
    if (!read_settings(asset.source, settings, error) ||
        !editor::asset_pipeline::read_bytes(asset.source, bytes, error))
    {
        return false;
    }
    if (bytes.empty() || bytes.size() > maximum_source_size)
    {
        error = "font source must be nonempty and at most 32 MiB";
        return false;
    }
    FontSource source;
    if (FT_Init_FreeType(&source.library) != 0 ||
        FT_New_Memory_Face(source.library, reinterpret_cast<const FT_Byte*>(bytes.data()),
                           static_cast<FT_Long>(bytes.size()),
                           static_cast<FT_Long>(settings.face_index), &source.face) != 0)
    {
        error = "font source or selected face could not be opened";
        return false;
    }
    schema::FontDataT font;
    if (!convert_font(source.face, settings, font, error))
    {
        return false;
    }
    if (!FontResourceCodec::validate(font))
    {
        error = "converted font failed runtime format validation";
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishFontDataBuffer(builder, schema::FontData::Pack(builder, &font));
    if (builder.GetSize() > 64U * 1024U * 1024U - 65536U)
    {
        error = "compiled font exceeds the resource payload size limit";
        return false;
    }
    return editor::asset_pipeline::write_bytes(
        context.output / asset.guid,
        std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())), error);
}

} // namespace engine::plugins::font_import
