#include "runtime/scene/text_component.hpp"

#include "runtime/scene/ecs_storage.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/resource_system.hpp"

#include <cmath>
#include <utility>

namespace engine
{

bool TextComponent::register_type(ComponentRegistry& registry)
{
    return register_component_storage<TextComponent, Data, Runtime>(registry);
}

TextComponent::Runtime* TextComponent::runtime() const noexcept
{
    return runtime_state<Runtime>();
}

bool TextComponent::initialize()
{
    if (!writable())
    {
        return false;
    }
    auto* value = write_data<Data>();
    if (!value)
    {
        return false;
    }
    value->shader_path = "builtin://shaders/font";
    modified();
    return true;
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

schema::TextDataT TextComponent::settings() const noexcept
{
    const auto* value = read_data<Data>();
    return value ? *value : Data{};
}

bool TextComponent::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool TextComponent::set_settings(const schema::TextDataT& settings)
{
    if (!writable())
    {
        return false;
    }
    if (!valid_settings(settings))
    {
        return false;
    }
    ResourcePtr font = runtime()->font_;
    ResourcePtr shader = runtime()->shader_;
    auto normalized = settings;
    if (normalized.shader_path.empty())
    {
        normalized.shader_path = "builtin://shaders/font";
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
    *write_data<Data>() = std::move(normalized);
    modified();
    runtime()->font_ = std::move(font);
    runtime()->shader_ = std::move(shader);
    return true;
}

bool TextComponent::set_font(std::string_view path)
{
    if (!writable())
    {
        return false;
    }
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

std::shared_ptr<const Resource> TextComponent::font() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->font_;
}

std::shared_ptr<const Resource> TextComponent::shader() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->shader_;
}

bool TextComponent::restore_data(const schema::ComponentDataT& data)
{
    if (!data.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* settings = data.payload.AsTextData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    if (!restore_flags(data))
    {
        return false;
    }
    return true;
}

} // namespace engine
