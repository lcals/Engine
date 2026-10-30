#include "editor/asset_pipeline/components/text_codec.hpp"

#include "editor/asset_pipeline/toml_helpers.hpp"
#include "runtime/scene/text_component.hpp"

#include <array>
#include <utility>

namespace engine::editor::asset_pipeline
{

schema::ComponentPayload TextCodec::type() const noexcept
{
    return schema::ComponentPayload::TextData;
}

std::string_view TextCodec::name() const noexcept
{
    return "text";
}

bool TextCodec::validate(const schema::ComponentDataT& component, std::string& error) const
{
    const auto* value = component.payload.AsTextData();
    if (value == nullptr || !engine::TextComponent::valid_settings(*value) ||
        value->shader_path.empty())
    {
        error = "invalid text component settings";
        return false;
    }
    return true;
}

bool TextCodec::read(const toml::table& table, schema::ComponentDataT& component,
                     std::string& error) const
{
    schema::TextDataT value;
    value.shader_path = "builtin/shaders/font";
    const auto read_string = [&table, &error](std::string_view key, std::string& output)
    {
        if (!table.contains(key))
        {
            return true;
        }
        const auto parsed = table[key].value<std::string>();
        if (!parsed)
        {
            error = "text." + std::string(key) + " must be a string";
            return false;
        }
        output = *parsed;
        return true;
    };
    if (!read_string("font", value.font_guid) || !read_string("shader", value.shader_path) ||
        !read_string("text", value.text))
    {
        return false;
    }
    std::array<double, 4> color{1, 1, 1, 1};
    if ((table.contains("size") && !float_number(table.get("size"), value.size)) ||
        (table.contains("letter_spacing") &&
         !float_number(table.get("letter_spacing"), value.letter_spacing)) ||
        (table.contains("color") && !numbers(table.get("color"), color)))
    {
        error = "text size, spacing and color must contain finite numbers";
        return false;
    }
    for (double channel : color)
    {
        if (channel < 0.0 || channel > 1.0)
        {
            error = "text color channels must be between zero and one";
            return false;
        }
    }
    value.color = {static_cast<float>(color[0]), static_cast<float>(color[1]),
                   static_cast<float>(color[2]), static_cast<float>(color[3])};
    if (!engine::TextComponent::valid_settings(value) || value.shader_path.empty())
    {
        error = "invalid text component settings";
        return false;
    }
    component.payload.Set(std::move(value));
    return true;
}

void TextCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    const auto* value = component.payload.AsTextData();
    table.insert("type", "text");
    table.insert("font", value->font_guid);
    table.insert("shader", value->shader_path);
    table.insert("text", value->text);
    table.insert("size", value->size);
    table.insert("letter_spacing", value->letter_spacing);
    table.insert("color", toml::array{value->color.r(), value->color.g(), value->color.b(),
                                      value->color.a()});
}

} // namespace engine::editor::asset_pipeline
