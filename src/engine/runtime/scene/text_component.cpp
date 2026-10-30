#include "runtime/scene/text_component.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/resource_system.hpp"

#include <cmath>
#include <utility>

namespace engine
{

TextComponent::TextComponent(Node& node) : Component(node)
{
    schema::TextDataT settings;
    settings.shader_path = "builtin/shaders/font";
    mutable_data().payload.Set(std::move(settings));
}

bool TextComponent::valid_settings(const schema::TextDataT& settings) noexcept
{
    const auto& color = settings.color;
    const auto valid_color = [](float value)
    { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; };
    return (settings.font_guid.empty() || GuidMap::valid_guid(settings.font_guid)) &&
           std::isfinite(settings.size) && settings.size > 0.0F &&
           std::isfinite(settings.letter_spacing) && valid_color(color.r()) &&
           valid_color(color.g()) && valid_color(color.b()) && valid_color(color.a());
}

const schema::TextDataT& TextComponent::settings() const noexcept
{
    return *data().payload.AsTextData();
}

bool TextComponent::set_settings(const schema::TextDataT& settings)
{
    if (!valid_settings(settings))
    {
        return false;
    }
    ResourcePtr font = font_;
    ResourcePtr shader = shader_;
    auto normalized = settings;
    if (normalized.shader_path.empty())
    {
        normalized.shader_path = "builtin/shaders/font";
    }
    auto* resources = ResourceSystem::instance();
    if (settings.font_guid.empty())
    {
        font.reset();
    }
    else if (!font || font->info().guid != settings.font_guid)
    {
        if (resources == nullptr || !resources->loader().load(settings.font_guid, font) || !font ||
            font->font() == nullptr)
        {
            return false;
        }
    }
    if (!shader || shader->info().path != normalized.shader_path)
    {
        std::string guid;
        if (resources == nullptr || !ResourceSystem::path_to_guid(normalized.shader_path, guid) ||
            !resources->loader().load(guid, shader) || !shader || shader->shader() == nullptr)
        {
            return false;
        }
    }
    *mutable_data().payload.AsTextData() = std::move(normalized);
    font_ = std::move(font);
    shader_ = std::move(shader);
    return true;
}

bool TextComponent::set_font(std::string_view path)
{
    auto replacement = settings();
    if (path.empty())
    {
        replacement.font_guid.clear();
    }
    else if (!ResourceSystem::path_to_guid(path, replacement.font_guid))
    {
        return false;
    }
    return set_settings(replacement);
}

const std::shared_ptr<const Resource>& TextComponent::font() const noexcept
{
    return font_;
}

const std::shared_ptr<const Resource>& TextComponent::shader() const noexcept
{
    return shader_;
}

bool TextComponent::restore_data(const schema::ComponentDataT& data)
{
    const auto* settings = data.payload.AsTextData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(data.enabled);
    return true;
}

} // namespace engine
