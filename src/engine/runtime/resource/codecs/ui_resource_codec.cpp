#include "runtime/resource/codecs/ui_resource_codec.hpp"
#include "runtime/core/guid_map.hpp"
#include "runtime/resource/codecs/resource_decode.hpp"
#include <SDL3/SDL_log.h>
#include <algorithm>

#include "runtime/resource/types/font_resource.hpp"
#include "runtime/resource/types/shader_resource.hpp"
#include "runtime/resource/types/texture_resource.hpp"
#include <set>
namespace engine
{
bool UiResourceCodec::valid_family(std::string_view family) noexcept
{
    return !family.empty() && family.size() <= 128 && family.front() != ' ' &&
           family.back() != ' ' &&
           std::all_of(family.begin(), family.end(),
                       [](unsigned char value)
                       {
                           return value >= 32 && value != 127 && !(value >= 'A' && value <= 'Z') &&
                                  value != ',' && value != '\'' && value != '"' && value != ';' &&
                                  value != '{' && value != '}' && value != '<' && value != '>';
                       });
}

schema::AssetKind UiResourceCodec::kind() const noexcept
{
    return schema::AssetKind::ui;
}

bool UiResourceCodec::decode(const schema::AssetEntryT& entry, ByteView payload,
                             std::unique_ptr<Resource>& output, std::string& error) const
{
    const auto validate = [](ByteView payload, std::string& error)
    {
        return resource_decode_detail::verify_flatbuffer(
            payload, schema::VerifyUiDocumentDataBuffer, "Invalid Ui FlatBuffer", error);
    };
    return resource_decode_detail::decode_flatbuffer<ResourceType>(
        entry, payload, 4U * 1024U * 1024U, validate, [](const std::uint8_t* bytes)
        { return schema::UnPackUiDocumentData(bytes); }, output, error);
}

bool UiResourceCodec::validate_data(const Resource& resource, std::string& error) const
{
    error.clear();
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data || data->schema_version != 1 || data->markup.empty() ||
        data->markup.size() > 1024U * 1024U || data->markup.find('\0') != std::string::npos ||
        data->fonts.empty() || data->fonts.size() > 64 || data->texture_guids.size() > 256)
    {
        error = "Invalid UI document";
        return false;
    }
    std::set<std::string_view> families;
    std::set<std::string_view> textures;
    for (const auto& binding : data->fonts)
    {
        if (!binding || !valid_family(binding->family) ||
            !families.insert(binding->family).second || !declares(resource, binding->font_guid) ||
            !valid_path(binding->shader_path))
        {
            error = "Invalid UI font binding";
            return false;
        }
    }
    for (const auto& guid : data->texture_guids)
    {
        if (!textures.insert(guid).second || !declares(resource, guid))
        {
            error = "Invalid UI texture binding";
            return false;
        }
    }
    return true;
}

bool UiResourceCodec::validate_dependencies(const Resource& resource, ResourceView dependencies,
                                            std::string& error) const
{
    error.clear();
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data)
    {
        error = "Invalid UI type";
        return false;
    }
    std::set<std::string_view> expected;
    for (const auto& binding : data->fonts)
    {
        const auto* font = dependency(dependencies, binding->font_guid);
        const auto* shader = dependency(dependencies, binding->shader_path);
        if (!resource_detail::data<FontResource>(font) ||
            !resource_detail::data<ShaderResource>(shader))
        {
            error = "Missing UI font or shader";
            return false;
        }
        expected.insert(font->info().guid);
        expected.insert(shader->info().guid);
    }
    for (const auto& guid : data->texture_guids)
    {
        const auto* texture =
            resource_detail::data<TextureResource>(dependency(dependencies, guid));
        if (!texture || texture->cube_map || texture->format == schema::TextureFormat::rgba16f ||
            texture->format == schema::TextureFormat::rg16f ||
            (texture->encoding != schema::TextureEncoding::native &&
             texture->encoding != schema::TextureEncoding::basis))
        {
            error = "UI requires 2D LDR native/Basis textures";
            return false;
        }
        expected.insert(guid);
    }
    return expected.size() == dependencies.size();
}
} // namespace engine
