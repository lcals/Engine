#include "resource_fixture.hpp"

#include "runtime/engine_config.hpp"
#include "runtime/font/font_system.hpp"
#include "runtime/resource/codecs/font_resource_codec.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/text_component.hpp"
#include "runtime/world/world_system.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string_view>

namespace
{

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "font system: %s\n", message);
    }
    return condition;
}

bool near(float actual, float expected)
{
    return std::abs(actual - expected) < 0.001F;
}

engine::schema::FontDataT font_data()
{
    engine::schema::FontDataT data;
    data.family = "Test font";
    data.style = "Regular";
    data.units_per_em = 1000;
    data.ascender = 0.8F;
    data.descender = -0.2F;
    data.line_gap = 0.2F;
    data.underline_thickness = 0.05F;
    for (std::uint32_t codepoint : {0U, 32U, 65U, 86U, 233U})
    {
        auto glyph = std::make_unique<engine::schema::FontGlyphT>();
        glyph->codepoint = codepoint;
        glyph->glyph_index = codepoint;
        glyph->advance_x = codepoint == 32 ? 0.25F : 0.6F;
        glyph->curve_offset = static_cast<std::uint32_t>(data.curves.size());
        if (codepoint != 32)
        {
            glyph->max_x = 0.5F;
            glyph->max_y = 0.7F;
            glyph->curve_count = 4;
            data.curves.emplace_back(0.0F, 0.0F, 0.25F, 0.0F, 0.5F, 0.0F);
            data.curves.emplace_back(0.5F, 0.0F, 0.5F, 0.35F, 0.5F, 0.7F);
            data.curves.emplace_back(0.5F, 0.7F, 0.25F, 0.7F, 0.0F, 0.7F);
            data.curves.emplace_back(0.0F, 0.7F, 0.0F, 0.35F, 0.0F, 0.0F);
        }
        data.glyphs.push_back(std::move(glyph));
    }
    data.kerning.emplace_back('A', 'V', -0.1F);
    return data;
}

bool publish_font(test_support::ResourceFixture& fixture, engine::ResourceSystem& resources,
                  const engine::schema::FontDataT& data)
{
    if (fixture.root.empty())
    {
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    engine::schema::FinishFontDataBuffer(builder, engine::schema::FontData::Pack(builder, &data));
    auto entry = std::make_unique<engine::schema::AssetEntryT>();
    entry->guid = "f0000000-0000-4000-8000-000000000001";
    entry->path = "project/fonts/test.ttf";
    entry->kind = engine::schema::AssetKind::font;
    engine::ByteBuffer entity;
    const engine::ByteView payload(reinterpret_cast<const std::byte*>(builder.GetBufferPointer()),
                                   builder.GetSize());
    if (!engine::ResourceSystem::encode_resource(entry->guid, entry->kind, payload, entity,
                                                 entry->header_size))
    {
        return false;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : entity)
    {
        hash = (hash ^ std::to_integer<std::uint8_t>(byte)) * 1099511628211ULL;
    }
    char digest[17]{};
    std::snprintf(digest, sizeof(digest), "%016llx", static_cast<unsigned long long>(hash));
    entry->content_hash = digest;
    entry->content_size = entity.size();
    entry->location = std::make_unique<engine::schema::FileLocationT>();
    entry->location->mount = "content";
    entry->location->path = "gen/" + entry->guid;
    entry->location->length = entity.size();
    std::ofstream stream(fixture.root / entry->location->path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(entity.data()),
                 static_cast<std::streamsize>(entity.size()));
    stream.close();
    if (!stream)
    {
        return false;
    }
    fixture.catalog.entries.push_back(std::move(entry));
    return fixture.publish(resources);
}

} // namespace

int main()
{
    const auto data = font_data();
    const auto* a = engine::FontSystem::glyph(data, 'A');
    const auto* fallback = engine::FontSystem::glyph(data, 0x1f680);
    if (!expect(engine::FontResourceCodec::validate(data), "fixture font must be valid") ||
        !expect(a != nullptr && a->codepoint == 'A', "lookup must preserve codepoint") ||
        !expect(fallback != nullptr && fallback->codepoint == 0, "missing glyph uses notdef") ||
        !expect(near(engine::FontSystem::measure(data, "AV", 10), 11), "kerning affects width") ||
        !expect(near(engine::FontSystem::measure(data, "AV", 10, 2), 13),
                "letter spacing appears between characters, not after the last") ||
        !expect(near(engine::FontSystem::measure(data, "A\nAV", 10), 11),
                "multiline width uses the widest line") ||
        !expect(near(engine::FontSystem::measure(data, "\xc3\xa9", 10), 6),
                "UTF-8 sequences produce one glyph") ||
        !expect(near(engine::FontSystem::measure(data, "\xff", 10), 6),
                "invalid UTF-8 uses the replacement glyph") ||
        !expect(engine::FontSystem::measure(data, "AV", 0) == 0,
                "invalid sizes must not produce widths"))
    {
        return 1;
    }
    engine::FontGeometry quad;
    if (!expect(engine::FontSystem::append_glyph(data, *a, 5, 8, 10, 0x80402010, true, quad),
                "UI glyph geometry") ||
        !expect(quad.vertices.size() == 4 && quad.indices.size() == 6,
                "one padded quad per glyph") ||
        !expect(near(quad.vertices[0].position[0], 4) && near(quad.vertices[0].position[1], 9),
                "UI quad must retain one pixel anti-alias padding") ||
        !expect(near(quad.vertices[2].position[1], 0), "UI axis must point down") ||
        !expect(quad.vertices[0].color == 0x80402010, "premultiplied colour is preserved") ||
        !expect(quad.vertices[0].curve_range[0] == a->curve_offset &&
                    quad.vertices[0].curve_range[1] == 4,
                "quad addresses only its glyph curves"))
    {
        return 1;
    }
    const auto old_size = quad.vertices.size();
    if (!expect(!engine::FontSystem::append_glyph(data, *a, 0, 0, -1, 0, false, quad) &&
                    quad.vertices.size() == old_size,
                "failed append must preserve prior geometry"))
    {
        return 1;
    }
    engine::ResourceSystem resources;
    engine::FontSystem fonts;
    test_support::ResourceFixture fixture;
    engine::ResourcePtr font;
    if (!expect(resources.initialize({}) && fonts.initialize({}), "CPU systems initialize") ||
        !expect(publish_font(fixture, resources, data), "publish independent font resource") ||
        !expect(fonts.load_font("project/fonts/test.ttf", font), "load font through catalog"))
    {
        return 1;
    }
    engine::FontLayout settings;
    settings.size = 10;
    engine::FontGeometry geometry;
    if (!expect(engine::FontSystem::layout(*font, "AV\nA", settings, geometry),
                "layout independent font resource") ||
        !expect(geometry.vertices.size() == 12 && geometry.indices.size() == 18,
                "newlines have no quad") ||
        !expect(near(geometry.width, 11) && near(geometry.height, 24),
                "layout dimensions include kerning and line metrics") ||
        !expect(fonts.gpu_resource_count() == 0, "CPU layout must not initialize GPU resources"))
    {
        return 1;
    }
    settings.size = std::numeric_limits<float>::quiet_NaN();
    if (!expect(!engine::FontSystem::layout(*font, "A", settings, geometry) &&
                    geometry.vertices.empty() && geometry.indices.empty(),
                "failed layout clears output"))
    {
        return 1;
    }
    engine::schema::TextDataT component;
    component.size = 1;
    component.color = {1, 1, 1, 1};
    if (!expect(engine::TextComponent::valid_settings(component),
                "empty component can be configured before font assignment"))
    {
        return 1;
    }
    component.size = 0;
    if (!expect(!engine::TextComponent::valid_settings(component),
                "text component rejects zero size"))
    {
        return 1;
    }
    engine::ResourcePtr missing = font;
    if (!expect(!fonts.load_font("project/fonts/missing.ttf", missing) && !missing,
                "failed font load resets output"))
    {
        return 1;
    }
    engine::schema::ShaderDataT shader_data;
    shader_data.path = "project/shaders/font.shader";
    shader_data.vertex_code = {1};
    shader_data.fragment_code = {2};
    engine::WorldSystem world;
    if (!expect(fixture.add(shader_data) && fixture.publish(resources) && world.initialize({}),
                "publish text shader and initialize scene"))
    {
        return 1;
    }
    auto* scene = world.create_scene("Text resource lifetime");
    auto* node = scene == nullptr ? nullptr : scene->create_node("Ordinary text node");
    auto* text = node == nullptr ? nullptr : node->add_component<engine::TextComponent>();
    component.size = 1;
    component.font_guid = font->info().guid;
    component.shader_path = shader_data.path;
    if (!expect(text != nullptr && text->set_settings(component) && text->shader(),
                "text component retains its loaded shader"))
    {
        return 1;
    }
    const auto held_shader = text->shader();
    fixture.catalog.entries.pop_back();
    shader_data.fragment_code = {3};
    engine::ResourcePtr newer_shader;
    if (!expect(fixture.add(shader_data) && fixture.publish(resources) &&
                    fixture.load(shader_data.path, newer_shader) && newer_shader != held_shader,
                "catalog update creates a new shader generation"))
    {
        return 1;
    }
    engine::FontDraw draw;
    draw.shader = text->shader();
    if (!expect(draw.shader == held_shader && draw.shader->shader()->fragment_code[0] == 2,
                "existing text draws with retained shader, not current catalog generation"))
    {
        return 1;
    }
    return 0;
}
