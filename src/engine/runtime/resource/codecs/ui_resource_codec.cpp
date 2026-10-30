#include "runtime/resource/codecs/ui_resource_codec.hpp"
#include "runtime/resource/resource_catalog.hpp"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <flatbuffers/verifier.h>
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

bool UiResourceCodec::decode(const ResourceCatalog& catalog, ByteView payload,
                             Resource& output) const
{
    if (payload.empty() || payload.size() > 4U * 1024U * 1024U)
    {
        return false;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
    flatbuffers::Verifier verifier(bytes, payload.size());
    if (!schema::VerifyUiDocumentDataBuffer(verifier))
    {
        return false;
    }
    auto data = schema::UnPackUiDocumentData(bytes);
    if (data->schema_version != 1 || data->markup.empty() || data->markup.size() > 1024U * 1024U ||
        data->markup.find('\0') != std::string::npos || data->fonts.empty() ||
        data->fonts.size() > 64 || data->texture_guids.size() > 256)
    {
        return false;
    }
    std::set<std::string> dependencies;
    std::set<std::string> families;
    for (const auto& binding : data->fonts)
    {
        Guid shader;
        if (!binding || !valid_family(binding->family) ||
            !families.insert(binding->family).second ||
            !declares(catalog, output, binding->font_guid, schema::AssetKind::font) ||
            !catalog.identities().path_to_guid(binding->shader_path, shader) ||
            !declares(catalog, output, shader, schema::AssetKind::shader))
        {
            return false;
        }
        dependencies.insert(binding->font_guid);
        dependencies.insert(std::move(shader));
    }
    for (const auto& guid : data->texture_guids)
    {
        if (!dependencies.insert(guid).second ||
            !declares(catalog, output, guid, schema::AssetKind::texture))
        {
            return false;
        }
    }
    if (dependencies.size() != output.info().dependencies.size())
    {
        return false;
    }
    store(output, std::move(data));
    return true;
}
bool UiResourceCodec::validate_dependencies(const ResourceCatalog& catalog,
                                            const Resource& resource,
                                            std::span<const ResourcePtr> dependencies) const
{
    const auto* data = resource.ui();
    if (!data)
    {
        return false;
    }
    for (const auto& binding : data->fonts)
    {
        const auto* font = dependency(dependencies, binding->font_guid);
        Guid shader_guid;
        if (!font || !font->font() ||
            !catalog.identities().path_to_guid(binding->shader_path, shader_guid))
        {
            return false;
        }
        const auto* shader = dependency(dependencies, shader_guid);
        if (!shader || !shader->shader())
        {
            return false;
        }
    }
    for (const auto& guid : data->texture_guids)
    {
        const auto* texture = dependency(dependencies, guid);
        if (!texture || !texture->texture())
        {
            return false;
        }
        const auto& data = *texture->texture();
        if (data.cube_map || (data.encoding != schema::TextureEncoding::rgba8 &&
                              data.encoding != schema::TextureEncoding::basis))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "UI requires 2D LDR rgba8/Basis textures: %s",
                         texture->info().path.c_str());
            return false;
        }
    }
    return true;
}
} // namespace engine
