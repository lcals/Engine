#include "runtime/scene/text_component.hpp"

#include "runtime/resource/types/font_resource.hpp"
#include "runtime/resource/types/shader_resource.hpp"
#include "runtime/scene/text_component_runtime.hpp"

#include "runtime/scene/ecs_storage.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/scene/resource_binding.hpp"

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
    Data output{};
    (void)get_settings(output);
    return output;
}

bool TextComponent::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool TextComponent::set_settings(const schema::TextDataT& settings)
{
    return apply_settings(settings, {});
}

bool TextComponent::apply_settings(const Data& settings,
                                   resource_detail::ResourceReference prepared)
{
    if (!writable())
    {
        return false;
    }
    if (!valid_settings(settings))
    {
        return false;
    }
    resource_detail::ResourceReference font;
    resource_detail::ResourceReference shader;
    if ((runtime()->font_ && !font.retain(runtime()->font_)) ||
        (runtime()->shader_ && !shader.retain(runtime()->shader_)))
    {
        return false;
    }
    auto normalized = settings;
    if (normalized.shader_path.empty())
    {
        normalized.shader_path = "builtin://shaders/font";
    }
    auto* resources = node().world()->resource_loader();
    if (settings.font_guid.empty())
    {
        font.reset();
    }
    else if (!font || font->info().guid != settings.font_guid)
    {
        font = std::move(prepared);
        if (resources == nullptr ||
            (!font && !ecs_detail::acquire_resource(node(), settings.font_guid, font)) || !font ||
            font.data<FontResource>() == nullptr)
        {
            return false;
        }
    }
    if (!shader || shader->info().path != normalized.shader_path)
    {
        shader.reset();
        if (resources == nullptr ||
            !ecs_detail::acquire_resource(node(), normalized.shader_path, shader) || !shader ||
            shader.data<ShaderResource>() == nullptr)
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
    resource_detail::ResourceReference prepared;
    auto replacement = settings();
    if (path.empty())
    {
        replacement.font_guid.clear();
    }
    else if (!ecs_detail::acquire_resource(node(), path, prepared))
    {
        return false;
    }
    if (prepared)
    {
        replacement.font_guid = prepared->info().guid;
    }
    return apply_settings(replacement, std::move(prepared));
}

ResourceHandle TextComponent::font() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->font_.handle();
}

ResourceHandle TextComponent::shader() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->shader_.handle();
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
