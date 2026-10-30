#include "runtime/resource/codecs/material_resource_codec.hpp"

#include "runtime/resource/codecs/material_value_utils.hpp"
#include "runtime/resource/resource_catalog.hpp"
#include "runtime/resource/resource_system.hpp"

#include <flatbuffers/verifier.h>

#include <set>
#include <utility>

namespace engine
{

bool MaterialResourceCodec::valid_shape(const schema::MaterialDataT& data)
{
    if (!valid_path(data.type_path) || !valid_path(data.shader_path) || data.schema_version != 1 ||
        (data.cull_mode != schema::CullMode::back && data.cull_mode != schema::CullMode::front &&
         data.cull_mode != schema::CullMode::none) ||
        (data.type_path.starts_with("builtin/") && data.shader_path.starts_with("project/")))
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
            (data.type_path.starts_with("builtin/") &&
             !texture->texture_path.starts_with("builtin/")))
        {
            return false;
        }
    }
    return true;
}

bool MaterialResourceCodec::validate(const schema::MaterialDataT& data,
                                     const schema::ShaderDataT* shader)
{
    if (!valid_shape(data) || shader == nullptr)
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
        const auto sampler = std::find_if(
            shader->samplers.begin(), shader->samplers.end(), [&texture](const auto& item)
            { return item && item->name == texture->name && item->exposed; });
        if (sampler == shader->samplers.end())
        {
            return false;
        }
    }
    return true;
}

bool MaterialResourceCodec::decode(const ResourceCatalog& catalog, ByteView payload,
                                   Resource& output) const
{
    const auto& entry = output.info();
    std::unique_ptr<schema::MaterialDataT> data;
    if (entry.path.starts_with("builtin/"))
    {
        const auto* builtin = ResourceSystem::find_material(entry.path);
        if (!builtin)
        {
            return false;
        }
        data = std::make_unique<schema::MaterialDataT>(*builtin);
    }
    else
    {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        flatbuffers::Verifier verifier(bytes, payload.size());
        if (!schema::VerifyMaterialDataBuffer(verifier))
        {
            return false;
        }
        data = schema::UnPackMaterialData(bytes);
    }
    if (entry.path.starts_with("builtin/"))
    {
        if (entry.dependencies.empty())
        {
            return false;
        }
        data->shader_guid = entry.dependencies.front();
    }
    std::string shader_path;
    if (!declares(catalog, output, data->shader_guid, schema::AssetKind::shader) ||
        !catalog.identities().guid_to_path(data->shader_guid, shader_path) ||
        (!data->shader_path.empty() && data->shader_path != shader_path) ||
        data->type_path != entry.path)
    {
        return false;
    }
    data->shader_path = shader_path;
    if (!valid_shape(*data))
    {
        return false;
    }
    for (const auto& binding : data->textures)
    {
        const auto* texture = catalog.find(binding->texture_guid);
        if (!declares(catalog, output, binding->texture_guid, schema::AssetKind::texture) ||
            !texture || texture->path != binding->texture_path)
        {
            return false;
        }
    }
    store(output, std::move(data));
    return true;
}

bool MaterialResourceCodec::validate_dependencies(const ResourceCatalog&, const Resource& resource,
                                                  std::span<const ResourcePtr> dependencies) const
{
    const auto* material = resource.material();
    if (!material)
    {
        return false;
    }
    const auto* shader = dependency(dependencies, material->shader_guid);
    if (!shader || !validate(*material, shader->shader()))
    {
        return false;
    }
    for (const auto& binding : material->textures)
    {
        const auto* texture = dependency(dependencies, binding->texture_guid);
        if (!texture || !texture->texture() || texture->info().path != binding->texture_path)
        {
            return false;
        }
    }
    return true;
}

} // namespace engine
