#include "runtime/resource/codecs/material_resource_codec.hpp"
#include "runtime/resource/codecs/resource_decode.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/codecs/material_value_utils.hpp"
#include "runtime/resource/types/shader_resource.hpp"
#include "runtime/resource/types/texture_resource.hpp"
#include <set>

#include <utility>

namespace engine
{

bool MaterialResourceCodec::valid_shape(const schema::MaterialDataT& data)
{
    if (!valid_path(data.type_path) || !valid_path(data.shader_path) || data.schema_version != 1 ||
        (data.cull_mode != schema::CullMode::back && data.cull_mode != schema::CullMode::front &&
         data.cull_mode != schema::CullMode::none) ||
        (data.blend_mode < schema::BlendMode::MIN || data.blend_mode > schema::BlendMode::MAX) ||
        (data.type_path.starts_with("builtin://") && !data.shader_path.starts_with("builtin://")))
    {
        return false;
    }
    std::set<std::string_view> names;
    for (const auto& parameter : data.parameters)
    {
        std::array<double, 4> values{};
        std::uint8_t count = 0;
        if (!parameter || parameter->name.empty() ||
            parameter->name.find('\0') != std::string::npos ||
            !names.insert(parameter->name).second ||
            !resource_codec_detail::parameter_values(parameter->value, values, count) ||
            !std::all_of(values.begin(), values.begin() + count,
                         [](double value)
                         {
                             return std::isfinite(value) &&
                                    std::abs(value) <= std::numeric_limits<float>::max();
                         }))
        {
            return false;
        }
    }
    for (const auto& texture : data.textures)
    {
        if (!texture || texture->name.empty() || texture->name.find('\0') != std::string::npos ||
            !names.insert(texture->name).second || !valid_path(texture->texture_path) ||
            (data.type_path.starts_with("builtin://") &&
             !texture->texture_path.starts_with("builtin://")))
        {
            return false;
        }
    }
    return true;
}

bool MaterialResourceCodec::validate(const schema::MaterialDataT& data,
                                     const schema::ShaderDataT* shader)
{
    if (!valid_shape(data) || shader == nullptr ||
        !resource_codec_detail::valid_builtin_pbr_options(data, *shader))
    {
        return false;
    }
    for (const auto& parameter : data.parameters)
    {
        const auto definition = std::find_if(shader->parameters.begin(), shader->parameters.end(),
                                             [&parameter](const auto& item)
                                             { return item && item->name == parameter->name; });
        if (definition == shader->parameters.end() || !(*definition)->exposed ||
            !resource_codec_detail::valid_parameter_value(**definition, parameter->value))
        {
            return false;
        }
    }
    for (const auto& texture : data.textures)
    {
        const auto sampler =
            std::find_if(shader->samplers.begin(), shader->samplers.end(),
                         [&texture](const auto& item)
                         {
                             return item && item->name == texture->name && item->exposed &&
                                    item->source == schema::ShaderTextureSource::resource;
                         });
        if (sampler == shader->samplers.end())
        {
            return false;
        }
    }
    return true;
}

schema::AssetKind MaterialResourceCodec::kind() const noexcept
{
    return schema::AssetKind::material;
}

bool MaterialResourceCodec::decode(const schema::AssetEntryT& entry, ByteView payload,
                                   std::unique_ptr<Resource>& output, std::string& error) const
{
    const auto validate = [](ByteView payload, std::string& error)
    {
        return resource_decode_detail::verify_flatbuffer(payload, schema::VerifyMaterialDataBuffer,
                                                         "Invalid Material FlatBuffer", error);
    };
    return resource_decode_detail::decode_flatbuffer<ResourceType>(
        entry, payload, 64U * 1024U * 1024U, validate,
        [](const std::uint8_t* bytes) { return schema::UnPackMaterialData(bytes); }, output, error);
}

bool MaterialResourceCodec::validate_data(const Resource& resource, std::string& error) const
{
    error.clear();
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data || data->type_path != resource.info().path || !valid_shape(*data) ||
        !declares(resource, data->shader_guid))
    {
        error = "Invalid material definition";
        return false;
    }
    std::set<std::string> expected{data->shader_guid};
    for (const auto& binding : data->textures)
    {
        if (!declares(resource, binding->texture_guid))
        {
            error = "Material texture is not declared";
            return false;
        }
        expected.insert(binding->texture_guid);
    }
    return expected.size() == resource.info().dependencies.size();
}

bool MaterialResourceCodec::validate_dependencies(const Resource& resource,
                                                  ResourceView dependencies,
                                                  std::string& error) const
{
    const auto* material = resource_detail::data<MaterialResource>(&resource);
    if (!material)
    {
        return false;
    }
    const auto* shader = dependency(dependencies, material->shader_guid);
    if (!shader || shader->info().path != material->shader_path ||
        !validate(*material, resource_detail::data<ShaderResource>(shader)))
    {
        return false;
    }
    for (const auto& binding : material->textures)
    {
        const auto* texture = dependency(dependencies, binding->texture_guid);
        if (!texture || !resource_detail::data<TextureResource>(texture) ||
            resource_detail::data<TextureResource>(texture)->cube_map ||
            texture->info().path != binding->texture_path)
        {
            return false;
        }
    }
    return true;
}

} // namespace engine
